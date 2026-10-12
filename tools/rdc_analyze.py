"""Analyse a RenderDoc capture (.rdc) of nhlport/shadPS4 running NHL 24.

Run with RenderDoc's embedded Python (qrenderdoc) or a Python matching RenderDoc's pymodules:
    qrenderdoc.exe --python tools\rdc_analyze.py <capture.rdc> <out_dir>
    python tools\rdc_analyze.py <capture.rdc> <out_dir>      (with <RenderDoc>\pymodules on PYTHONPATH)

Writes <out_dir>/actions.txt (one line per draw/dispatch/copy with targets and shaders), and
PNGs of every colour render target as it looks after the last action that wrote it, plus the
final presented image. Meant to show where the 3D scene turns black.
"""
import os
import sys
import traceback


def _main():

    try:
        import renderdoc as rd
    except ImportError:
        # qrenderdoc exposes the module as a builtin global named `renderdoc`
        rd = globals().get("renderdoc")
        if rd is None:
            raise

    # qrenderdoc --python does not forward script arguments: take them from the environment first.
    args = [os.environ.get("RDC_CAPTURE", ""), os.environ.get("RDC_OUT", "")]
    if not args[0] or not args[1]:
        args = sys.argv[1:]
    if len(args) < 2 or not args[0] or not args[1]:
        print(__doc__)
        sys.exit(2)
    capture_path, out_dir = args[0], args[1]
    os.makedirs(out_dir, exist_ok=True)

    rd.InitialiseReplay(rd.GlobalEnvironment(), [])
    cap = rd.OpenCaptureFile()
    res = cap.OpenFile(capture_path, "", None)
    if res != rd.ResultCode.Succeeded:
        raise RuntimeError("OpenFile failed: " + str(res))
    if not cap.LocalReplaySupport():
        raise RuntimeError("capture cannot be replayed locally")
    res, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
    if res != rd.ResultCode.Succeeded:
        raise RuntimeError("OpenCapture failed: " + str(res))

    textures = {t.resourceId: t for t in ctrl.GetTextures()}
    resnames = {r.resourceId: r.name for r in ctrl.GetResources()}


    def fmt_tex(tid):
        t = textures.get(tid)
        if t is None:
            return str(tid)
        return "%s %dx%dx%d L%d M%d %s" % (resnames.get(tid, str(tid)), t.width, t.height, t.depth,
                                           t.arraysize, t.mips, t.format.Name())


    lines = []
    last_writer = {}  # texture id -> eventId of last action that had it bound as output
    all_actions = []


    def walk(actions, depth):
        for a in actions:
            all_actions.append((a, depth))
            walk(a.children, depth + 1)


    walk(ctrl.GetRootActions(), 0)

    draw_flags = rd.ActionFlags.Drawcall | rd.ActionFlags.Dispatch | rd.ActionFlags.Clear | \
        rd.ActionFlags.Copy | rd.ActionFlags.Resolve | rd.ActionFlags.Present | rd.ActionFlags.MeshDispatch

    num_draws = num_dispatch = 0
    for a, depth in all_actions:
        if not (a.flags & draw_flags):
            continue
        ctrl.SetFrameEvent(a.eventId, True)
        ps = ctrl.GetPipelineState()
        outs = []
        if a.flags & rd.ActionFlags.Drawcall:
            num_draws += 1
            for i, o in enumerate(ps.GetOutputTargets()):
                if o.resource != rd.ResourceId.Null():
                    outs.append("RT%d=%s" % (i, fmt_tex(o.resource)))
                    last_writer[o.resource] = a.eventId
            d = ps.GetDepthTarget()
            if d.resource != rd.ResourceId.Null():
                outs.append("DS=%s" % fmt_tex(d.resource))
        if a.flags & rd.ActionFlags.Dispatch:
            num_dispatch += 1
            # storage images written by compute
            try:
                for rw in ps.GetReadWriteResources(rd.ShaderStage.Compute):
                    for r in rw.resources:
                        if r.resource in textures:
                            outs.append("UAV=%s" % fmt_tex(r.resource))
                            last_writer[r.resource] = a.eventId
            except Exception as e:  # API differences between versions
                outs.append("(rw query failed: %s)" % e)
        shaders = []
        for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Pixel, rd.ShaderStage.Compute):
            sid = ps.GetShader(stage)
            if sid != rd.ResourceId.Null():
                shaders.append("%s:%s" % (str(stage).split(".")[-1][:2], resnames.get(sid, str(sid))))
        lines.append("%5d %s%-32s %s | %s | %s" % (
            a.eventId, "  " * depth, a.GetName(ctrl.GetStructuredFile())[:32],
            "inst=%d idx=%d grp=%s" % (a.numInstances, a.numIndices, list(a.dispatchDimension)),
            " ".join(shaders), " ".join(outs)))

    with open(os.path.join(out_dir, "actions.txt"), "w", encoding="utf-8") as f:
        f.write("# %d draws, %d dispatches, %d textures\n" % (num_draws, num_dispatch, len(textures)))
        f.write("\n".join(lines))
    print("actions: %d draws, %d dispatches -> %s" % (num_draws, num_dispatch,
                                                       os.path.join(out_dir, "actions.txt")))

    # Save every colour target after its last write, plus swapchain/presented images.
    saved = 0
    for tid, eid in sorted(last_writer.items(), key=lambda kv: kv[1]):
        t = textures.get(tid)
        if t is None or t.format.compType == rd.CompType.Depth:
            continue
        ctrl.SetFrameEvent(eid, True)
        ts = rd.TextureSave()
        ts.resourceId = tid
        ts.destType = rd.FileType.PNG
        ts.mip = 0
        ts.slice.sliceIndex = 0
        ts.alpha = rd.AlphaMapping.Discard
        ts.comp.blackPoint = 0.0
        ts.comp.whitePoint = 1.0
        name = "e%05d_%s_%dx%d_%s.png" % (eid, resnames.get(tid, str(tid)).replace("/", "_").replace(" ", "_")[:40],
                                         t.width, t.height, t.format.Name())
        ok = ctrl.SaveTexture(ts, os.path.join(out_dir, name))
        saved += 1 if ok else 0
        # HDR targets are also saved with a boosted range so dark content becomes visible
        if "SFLOAT" in t.format.Name() or "UFLOAT" in t.format.Name():
            ts.comp.whitePoint = 0.01
            ctrl.SaveTexture(ts, os.path.join(out_dir, "boost_" + name))
        # UI-style targets: keep the alpha channel too (what the composite blends with)
        if "R8G8B8A8" in t.format.Name():
            ts.comp.whitePoint = 1.0
            ts.alpha = rd.AlphaMapping.Preserve
            ctrl.SaveTexture(ts, os.path.join(out_dir, "alpha_" + name))
            ts.alpha = rd.AlphaMapping.Discard
    print("saved %d render target images to %s" % (saved, out_dir))

    ctrl.Shutdown()
    cap.Shutdown()
    rd.ShutdownReplay()



if __name__ == "__main__" or "pyrenderdoc" in globals() or True:
    _out = os.environ.get("RDC_OUT") or (sys.argv[2] if len(sys.argv) > 2 else ".")
    os.makedirs(_out, exist_ok=True)
    _log = open(os.path.join(_out, "analyze_log.txt"), "w", encoding="utf-8")
    sys.stdout = _log
    sys.stderr = _log
    try:
        _main()
        print("DONE")
    except SystemExit:
        print("EXIT")
    except Exception:
        traceback.print_exc()
        print("FAILED")
    _log.flush()
    _log.close()
