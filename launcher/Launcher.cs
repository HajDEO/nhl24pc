// NHL24 PC launcher. Built with the C# compiler that ships with Windows (.NET Framework 4.x, C# 5),
// see build_launcher.ps1. Two views:
//   Simple   – ready check (game files, saves, GPU, controls), quick settings, PKG installation,
//              PLAY button
//   Advanced – keyboard bindings over a DualShock 4 picture and the full display settings
// Writes the port's own files:
//   user\config.json              (GPU section: window size, fullscreen, present mode, FSR/RCAS;
//                                  General section: redzone_patches (Windows red-zone protection);
//                                  Launcher section: language)
//   user\input_config\default.ini (keyboard bindings; controller bindings stay 1:1)
//   user\game_path.txt            (eboot.bin of a game folder other than game\ next to the exe)
// PKG installation runs "nhl24.exe --extract-pkg <file> --extract-to <game folder>" and follows
// its "progress" lines (see nhlport/src/core/file_format/pkg_extract.h).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Management;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace NhlPort
{
    // ------------------------------------------------------------------ localisation
    public static class Loc
    {
        public static int Lang = 0; // 0 = Slovak, 1 = English
        static readonly Dictionary<string, string[]> texts = new Dictionary<string, string[]>
        {
            { "simple", new[] { "Jednoduchý", "Simple" } },
            { "advanced", new[] { "Rozšírený", "Advanced" } },
            { "ready", new[] { "KONTROLA PRIPRAVENOSTI", "READY CHECK" } },
            { "quick", new[] { "RÝCHLE NASTAVENIA", "QUICK SETTINGS" } },
            { "game_ok", new[] { "Hra: NHL 24 v{0} v {1}", "Game: NHL 24 v{0} in {1}" } },
            { "game_upd", new[] { " + aktualizácia v{0}", " + update v{0}" } },
            { "game_missing", new[] { "Súbory hry sa nenašli – nainštaluj ich z PKG", "Game files not found – install them from a PKG" } },
            { "saves_ok", new[] { "Uložené pozície: {0} v {1}", "Saves: {0} in {1}" } },
            { "saves_none", new[] { "Zatiaľ žiadne uložené pozície: hra ich vytvorí v {0}", "No saves yet: the game creates them in {0}" } },
            { "gpu", new[] { "Grafická karta: {0}", "Graphics card: {0}" } },
            { "gpu_unknown", new[] { "Grafická karta: nezistená", "Graphics card: not detected" } },
            { "controls_default", new[] { "Ovládanie: predvolené klávesy, gamepad 1:1 (upraviť v Rozšírenom)", "Controls: default keys, gamepad 1:1 (edit in Advanced)" } },
            { "controls_custom", new[] { "Ovládanie: vlastné klávesy, gamepad 1:1", "Controls: custom keys, gamepad 1:1" } },
            { "open_folder", new[] { "Otvoriť priečinok", "Open folder" } },
            { "hint", new[] { "Gamepad (Xbox / DualShock) funguje automaticky; klávesy sa dajú zmeniť v Rozšírenom. Diagnostika hry je vypnutá (zapne ju premenná NHLPORT_DIAG=1).", "A gamepad (Xbox / DualShock) works automatically; keys can be changed in Advanced. Game diagnostics are off (NHLPORT_DIAG=1 enables them)." } },
            { "game_folder", new[] { "Priečinok hry", "Game folder" } },
            { "browse", new[] { "Prehľadávať…", "Browse…" } },
            { "install_pkg", new[] { "Inštalovať z PKG…", "Install from PKG…" } },
            { "pkg_req", new[] { "Potrebuje PKG základnej hry NHL 24 v01.00 (CUSA37933). Update neinštaluj: s v1.6 hra zamrzne pri prvom vhadzovaní.",
                                  "Needs the NHL 24 base game PKG v01.00 (CUSA37933). Do not install the update: v1.6 freezes at the first puck drop." } },
            { "output", new[] { "Výstup", "Output" } },
            { "display_mode", new[] { "Režim zobrazenia", "Display mode" } },
            { "windowed", new[] { "Okno", "Windowed" } },
            { "borderless", new[] { "Celá obrazovka (bez rámu)", "Borderless fullscreen" } },
            { "exclusive", new[] { "Celá obrazovka (exkluzívna)", "Exclusive fullscreen" } },
            { "upscaler", new[] { "Upscaler", "Upscaler" } },
            { "ups_off", new[] { "Vypnutý (bilineárne)", "Off (bilinear)" } },
            { "ups_fsr", new[] { "FSR", "FSR" } },
            { "ups_fsr_rcas", new[] { "FSR + RCAS zaostrenie", "FSR + RCAS sharpening" } },
            { "sharpness", new[] { "Zaostrenie", "Sharpness" } },
            { "vsync", new[] { "VSync", "VSync" } },
            { "vsync_desc", new[] { "Synchronizácia s obnovovacou frekvenciou (bez trhania, mierne vyššia odozva).", "Synchronises with the refresh rate (no tearing, slightly more input lag)." } },
            { "redzone", new[] { "Ochrana red zone (Windows)", "Red zone protection (Windows)" } },
            { "redzone_desc", new[] { "Experimentálna oprava pádu pri načítaní zápasu (chyba 0xc0000005 hneď po výbere tímov). Zapni, ak ti hra takto padá; môže mierne znížiť výkon. Platí od ďalšieho spustenia.",
                                      "Experimental fix for the crash while a match loads (error 0xc0000005 right after team selection). Turn it on if the game crashes like that; may cost a little performance. Takes effect on the next start." } },
            { "redzone_short", new[] { "Red zone ✓", "Red zone ✓" } },
            { "lang", new[] { "Jazyk launchera", "Launcher language" } },
            { "play", new[] { "HRAŤ", "PLAY" } },
            { "save", new[] { "Uložiť", "Save" } },
            { "saved", new[] { "Uložené", "Saved" } },
            { "no_exe", new[] { "nhl24.exe sa nenašiel vedľa launchera:\n{0}", "nhl24.exe was not found next to the launcher:\n{0}" } },
            { "save_fail", new[] { "Nepodarilo sa uložiť nastavenia:\n{0}", "Could not save the settings:\n{0}" } },
            { "pkg_title", new[] { "Inštalácia PKG", "PKG installation" } },
            { "pkg_reading", new[] { "Čítam balík…", "Reading the package…" } },
            { "pkg_extracting", new[] { "Rozbaľujem {0}", "Extracting {0}" } },
            { "pkg_progress", new[] { "{0} / {1} MB   •   {2} / {3} súborov", "{0} / {1} MB   •   {2} / {3} files" } },
            { "pkg_done", new[] { "Hotovo: {0} nainštalovaný do {1}", "Done: {0} installed into {1}" } },
            { "pkg_error", new[] { "Inštalácia zlyhala: {0}", "Installation failed: {0}" } },
            { "pkg_wrong", new[] { "Tento balík je {0} ({1}), port potrebuje NHL 24 (CUSA37933).", "This package is {0} ({1}); the port needs NHL 24 (CUSA37933)." } },
            { "pkg_kind_base", new[] { "základná hra", "base game" } },
            { "pkg_kind_patch", new[] { "aktualizácia", "update" } },
            { "pkg_kind_other", new[] { "iný obsah", "other content" } },
            { "pkg_confirm", new[] { "{0}: {1} v{2}, {3} MB.\n\nCieľ: {4}\n\nPokračovať?", "{0}: {1} v{2}, {3} MB.\n\nTarget: {4}\n\nContinue?" } },
            { "cancel", new[] { "Zrušiť", "Cancel" } },
            { "close", new[] { "Zavrieť", "Close" } },
            { "cancelled", new[] { "Inštalácia zrušená.", "Installation cancelled." } },
            { "tab_controls", new[] { "Ovládanie", "Controls" } },
            { "tab_display", new[] { "Zobrazenie", "Display" } },
            { "pad_hint", new[] { "Klikni na tlačidlo na obrázku (alebo riadok vpravo) a stlač klávesu, ktorú chceš priradiť. Esc = zrušiť. Gamepad (Xbox / DualShock) funguje automaticky, tlačidlá sú mapované 1:1.",
                                   "Click a button in the picture (or a row on the right) and press the key to assign. Esc = cancel. A gamepad (Xbox / DualShock) works automatically, buttons map 1:1." } },
            { "col_action", new[] { "Akcia (PS4)", "Action (PS4)" } },
            { "col_key", new[] { "Klávesa", "Key" } },
            { "defaults", new[] { "Predvolené klávesy", "Default keys" } },
            { "press_key", new[] { "Stlač klávesu pre: {0}   (Esc = zrušiť)", "Press a key for: {0}   (Esc = cancel)" } },
            { "key_unusable", new[] { "Túto klávesu nie je možné použiť, skús inú.", "This key cannot be used, try another one." } },
            { "resolution", new[] { "Rozlíšenie okna / obrazovky:", "Window / screen resolution:" } },
            { "fsr_group", new[] { "AMD FidelityFX Super Resolution (FSR)", "AMD FidelityFX Super Resolution (FSR)" } },
            { "fsr_box", new[] { "FSR upscaling na rozlíšenie okna", "FSR upscaling to the window resolution" } },
            { "rcas_box", new[] { "RCAS zaostrenie", "RCAS sharpening" } },
            { "display_note", new[] { "Hra vykresľuje natívne 1920x1080 (ako na PS4) a výstup sa škáluje na zvolené okno alebo obrazovku. FSR zlepší ostrosť pri väčších rozlíšeniach (1440p, 4K). Interné rozlíšenie hry sa nemení, aby sa nerozbilo rozloženie obrazu a menu.",
                                       "The game renders at its native 1920x1080 (as on the PS4) and the output is scaled to the chosen window or screen. FSR improves sharpness at larger resolutions (1440p, 4K). The internal resolution stays fixed so the picture and menu layout stay intact." } },
            { "version_line", new[] { "Windows  •  v{0}", "Windows  •  v{0}" } },
            { "game_line", new[] { "CUSA37933  •  verzia hry {0}", "CUSA37933  •  game version {0}" } },
            { "game_line_none", new[] { "CUSA37933  •  hra nie je nainštalovaná", "CUSA37933  •  game not installed" } },
        };

        public static string T(string key)
        {
            string[] v;
            if (texts.TryGetValue(key, out v)) return v[Math.Min(Lang, v.Length - 1)];
            return key;
        }

        public static string F(string key, params object[] args)
        {
            return string.Format(T(key), args);
        }
    }

    // ------------------------------------------------------------------ small controls
    public class GlassPanel : Panel
    {
        public Color Fill = Color.FromArgb(205, 10, 16, 26);
        public Color Border = Color.FromArgb(120, 255, 255, 255);
        public int Radius = 10;

        public GlassPanel()
        {
            SetStyle(ControlStyles.SupportsTransparentBackColor | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            BackColor = Color.Transparent;
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            base.OnPaintBackground(e); // parent's background (the arena picture) through the transparency
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            var rect = new Rectangle(0, 0, Width - 1, Height - 1);
            using (var path = Rounded(rect, Radius))
            using (var brush = new SolidBrush(Fill))
            using (var pen = new Pen(Border, 1f))
            {
                e.Graphics.FillPath(brush, path);
                e.Graphics.DrawPath(pen, path);
            }
        }

        public static GraphicsPath Rounded(Rectangle r, int radius)
        {
            var path = new GraphicsPath();
            int d = radius * 2;
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }
    }

    public class TransparentLabel : Label
    {
        public TransparentLabel()
        {
            SetStyle(ControlStyles.SupportsTransparentBackColor | ControlStyles.OptimizedDoubleBuffer, true);
            BackColor = Color.Transparent;
            ForeColor = Color.White;
        }
    }

    // Section heading: orange accent bar + bold italic upper-case title + thin rule.
    public class SectionTitle : Control
    {
        public SectionTitle()
        {
            SetStyle(ControlStyles.SupportsTransparentBackColor | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint, true);
            BackColor = Color.Transparent;
            Height = 46;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            e.Graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
            using (var accent = new SolidBrush(Theme.Orange))
            {
                var bar = new Point[] { new Point(6, 4), new Point(16, 4), new Point(10, 30), new Point(0, 30) };
                e.Graphics.FillPolygon(accent, bar);
            }
            using (var font = new Font("Segoe UI", 19f, FontStyle.Bold | FontStyle.Italic))
                TextRenderer.DrawText(e.Graphics, Text, font, new Point(26, 0), Color.White);
            using (var pen = new Pen(Color.FromArgb(90, 255, 255, 255)))
                e.Graphics.DrawLine(pen, 0, Height - 4, Width, Height - 4);
        }
    }

    // One row of the ready check: a check box icon (orange = ok, grey = missing) and a text.
    public class CheckRow : Control
    {
        public bool Ok = true;

        public CheckRow()
        {
            SetStyle(ControlStyles.SupportsTransparentBackColor | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint, true);
            BackColor = Color.Transparent;
            Height = 44;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            var box = new Rectangle(4, 6, 22, 22);
            using (var brush = new SolidBrush(Ok ? Theme.Orange : Color.FromArgb(80, 90, 100)))
            using (var path = GlassPanel.Rounded(box, 4))
                e.Graphics.FillPath(brush, path);
            if (Ok)
            {
                using (var pen = new Pen(Color.White, 2.5f))
                    e.Graphics.DrawLines(pen, new[] { new Point(9, 17), new Point(13, 22), new Point(21, 11) });
            }
            else
            {
                using (var pen = new Pen(Color.White, 2.5f))
                {
                    e.Graphics.DrawLine(pen, 10, 12, 20, 22);
                    e.Graphics.DrawLine(pen, 20, 12, 10, 22);
                }
            }
            var textRect = new Rectangle(36, 0, Width - 40, Height);
            TextRenderer.DrawText(e.Graphics, Text, Font, textRect, Color.White,
                TextFormatFlags.WordBreak | TextFormatFlags.VerticalCenter | TextFormatFlags.Left);
        }
    }

    // Transparent slider (a WinForms TrackBar cannot sit on the translucent panel without a box).
    public class GlassSlider : Control
    {
        int minimum = 0, maximum = 1000, current = 0;
        bool dragging;
        public event EventHandler ValueChanged;

        public GlassSlider()
        {
            SetStyle(ControlStyles.SupportsTransparentBackColor | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            BackColor = Color.Transparent;
            Cursor = Cursors.Hand;
            Height = 28;
            TabStop = true;
        }

        public int Minimum { get { return minimum; } set { minimum = value; Value = current; } }
        public int Maximum { get { return maximum; } set { maximum = value; Value = current; } }
        public int Value
        {
            get { return current; }
            set
            {
                int v = Math.Max(minimum, Math.Min(maximum, value));
                if (v == current) return;
                current = v;
                Invalidate();
                if (ValueChanged != null) ValueChanged(this, EventArgs.Empty);
            }
        }

        const int Pad = 10;

        float Fraction { get { return maximum > minimum ? (current - minimum) / (float)(maximum - minimum) : 0f; } }

        void SetFromX(int x)
        {
            float f = (x - Pad) / (float)Math.Max(1, Width - 2 * Pad);
            Value = minimum + (int)Math.Round(Math.Max(0f, Math.Min(1f, f)) * (maximum - minimum));
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (!Enabled || e.Button != MouseButtons.Left) return;
            dragging = true;
            Focus();
            SetFromX(e.X);
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            if (dragging) SetFromX(e.X);
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            base.OnMouseUp(e);
            dragging = false;
        }

        protected override bool IsInputKey(Keys keyData)
        {
            return keyData == Keys.Left || keyData == Keys.Right || base.IsInputKey(keyData);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            int step = Math.Max(1, (maximum - minimum) / 40);
            if (e.KeyCode == Keys.Left) { Value -= step; e.Handled = true; }
            else if (e.KeyCode == Keys.Right) { Value += step; e.Handled = true; }
        }

        protected override void OnEnabledChanged(EventArgs e)
        {
            base.OnEnabledChanged(e);
            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            int cy = Height / 2;
            int x0 = Pad, x1 = Width - Pad;
            int xv = x0 + (int)((x1 - x0) * Fraction);
            var trackColor = Enabled ? Color.FromArgb(90, 255, 255, 255) : Color.FromArgb(50, 255, 255, 255);
            var fillColor = Enabled ? Theme.Orange : Color.FromArgb(110, 110, 120);
            using (var brush = new SolidBrush(trackColor))
            using (var path = GlassPanel.Rounded(new Rectangle(x0, cy - 3, x1 - x0, 6), 3))
                e.Graphics.FillPath(brush, path);
            if (xv - x0 > 6)
            {
                using (var brush = new SolidBrush(fillColor))
                using (var path = GlassPanel.Rounded(new Rectangle(x0, cy - 3, xv - x0, 6), 3))
                    e.Graphics.FillPath(brush, path);
            }
            for (int i = 0; i <= 4; i++)
            {
                int tx = x0 + (x1 - x0) * i / 4;
                using (var pen = new Pen(Color.FromArgb(110, 255, 255, 255)))
                    e.Graphics.DrawLine(pen, tx, cy + 7, tx, cy + 11);
            }
            using (var brush = new SolidBrush(Enabled ? Color.White : Color.FromArgb(170, 170, 180)))
                e.Graphics.FillEllipse(brush, xv - 8, cy - 8, 16, 16);
            using (var pen = new Pen(fillColor, 2f))
                e.Graphics.DrawEllipse(pen, xv - 8, cy - 8, 16, 16);
        }
    }

    public class FlatButton : Button
    {
        public bool Accent = false;

        public FlatButton()
        {
            FlatStyle = FlatStyle.Flat;
            FlatAppearance.BorderSize = 1;
            FlatAppearance.BorderColor = Color.FromArgb(120, 140, 160);
            FlatAppearance.MouseOverBackColor = Color.FromArgb(50, 62, 80);
            FlatAppearance.MouseDownBackColor = Color.FromArgb(30, 38, 50);
            BackColor = Color.FromArgb(28, 36, 48);
            ForeColor = Color.White;
            Font = new Font("Segoe UI", 10f);
            Cursor = Cursors.Hand;
            Height = 34;
        }

        public void SetAccent(bool on)
        {
            Accent = on;
            BackColor = on ? Theme.Orange : Color.FromArgb(28, 36, 48);
            FlatAppearance.BorderColor = on ? Theme.Orange : Color.FromArgb(120, 140, 160);
            FlatAppearance.MouseOverBackColor = on ? Color.FromArgb(255, 150, 60) : Color.FromArgb(50, 62, 80);
        }
    }

    // The PLAY button: btn.png with a hover / pressed brightness change.
    public class ImageButton : Control
    {
        readonly Image image;
        bool hover, down;

        public ImageButton(Image img)
        {
            image = img;
            SetStyle(ControlStyles.SupportsTransparentBackColor | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint, true);
            BackColor = Color.Transparent;
            Cursor = Cursors.Hand;
            MouseEnter += delegate { hover = true; Invalidate(); };
            MouseLeave += delegate { hover = false; down = false; Invalidate(); };
            MouseDown += delegate { down = true; Invalidate(); };
            MouseUp += delegate { down = false; Invalidate(); };
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            e.Graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
            if (image == null)
            {
                using (var brush = new SolidBrush(Theme.Orange)) e.Graphics.FillRectangle(brush, ClientRectangle);
                TextRenderer.DrawText(e.Graphics, Text, new Font("Segoe UI", 24f, FontStyle.Bold | FontStyle.Italic), ClientRectangle, Color.White,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
                return;
            }
            float scale = down ? 0.97f : 1f;
            int w = (int)(Width * scale), h = (int)(Height * scale);
            var dest = new Rectangle((Width - w) / 2, (Height - h) / 2, w, h);
            using (var attrs = new ImageAttributes())
            {
                float b = hover ? 0.12f : 0f;
                var matrix = new ColorMatrix(new float[][] {
                    new float[] { 1, 0, 0, 0, 0 }, new float[] { 0, 1, 0, 0, 0 }, new float[] { 0, 0, 1, 0, 0 },
                    new float[] { 0, 0, 0, 1, 0 }, new float[] { b, b, b, 0, 1 } });
                attrs.SetColorMatrix(matrix);
                e.Graphics.DrawImage(image, dest, 0, 0, image.Width, image.Height, GraphicsUnit.Pixel, attrs);
            }
        }
    }

    public static class Theme
    {
        public static readonly Color Orange = Color.FromArgb(255, 122, 26);
        public static readonly Color Field = Color.FromArgb(22, 29, 40);
        public static readonly Color FieldText = Color.White;
        public static readonly Font Body = new Font("Segoe UI", 10.5f);
        public static readonly Font Small = new Font("Segoe UI", 9f);
        public static readonly Font Label = new Font("Segoe UI", 9.5f);

        public static void Style(ComboBox box)
        {
            box.FlatStyle = FlatStyle.Flat;
            box.BackColor = Field;
            box.ForeColor = FieldText;
            box.Font = Body;
        }

        public static void Style(TextBox box)
        {
            box.BorderStyle = BorderStyle.FixedSingle;
            box.BackColor = Field;
            box.ForeColor = FieldText;
            box.Font = Body;
        }
    }

    public class PadAction
    {
        public string Id;      // name used by the input config (cross, l1, axis_left_x_minus ...)
        public string Label;   // label
        public string Default; // default key name
        public int X, Y;       // marker position on the 1254x1254 pad picture
        public PadAction(string id, string label, string def, int x, int y)
        {
            Id = id; Label = label; Default = def; X = x; Y = y;
        }
    }

    // ------------------------------------------------------------------ the form
    public class LauncherForm : Form
    {
        const string FallbackVersion = "0.8.3";
        string PortVersion = FallbackVersion; // taken from the version resource of nhl24.exe when present
        const string TitleId = "CUSA37933";
        readonly string baseDir;
        readonly string configPath;
        readonly string inputPath;
        readonly string gamePathFile;
        readonly string gameExe;
        readonly string savesDir;

        Image background;
        Image playImage;
        Image padImage;

        readonly List<PadAction> actions = new List<PadAction>();
        readonly Dictionary<string, string> bindings = new Dictionary<string, string>();
        Dictionary<string, object> config;

        // simple view
        Panel simpleView;
        GlassPanel readyPanel, quickPanel, progressPanel;
        CheckRow rowGame, rowSaves, rowGpu, rowControls;
        TextBox gameFolderBox;
        ComboBox outputBox, displayModeBox, upscalerBox, langBox;
        GlassSlider sharpBar;
        TransparentLabel sharpValue, pkgReqLabel, summaryLabel, versionLabel, gameVersionLabel, statusLabel;
        CheckBox vsyncQuick;
        CheckBox redzoneQuick;
        ImageButton playButton;
        FlatButton simpleBtn, advancedBtn;
        // pkg progress
        TransparentLabel progressTitle, progressFile, progressNumbers;
        ProgressBar progressBar;
        FlatButton progressCancel;
        Process pkgProcess;
        string pkgTarget;
        string pkgKind;

        // advanced view
        GlassPanel advancedView;
        PictureBox padBox;
        ListView bindList;
        Label captureLabel;
        PadAction capturing;
        ComboBox resolutionBox;
        CheckBox fullscreenBox;
        ComboBox fullscreenModeBox;
        CheckBox vsyncBox;
        CheckBox fsrBox;
        CheckBox rcasBox;
        TrackBar sharpnessBar;
        Label sharpnessLabel;

        readonly bool startAdvanced;

        public LauncherForm(bool advanced)
        {
            startAdvanced = advanced;
            baseDir = AppDomain.CurrentDomain.BaseDirectory;
            configPath = Path.Combine(baseDir, "user", "config.json");
            inputPath = Path.Combine(baseDir, "user", "input_config", "default.ini");
            gamePathFile = Path.Combine(baseDir, "user", "game_path.txt");
            gameExe = Path.Combine(baseDir, "nhl24.exe");
            savesDir = Path.Combine(baseDir, "user", "home", "1000", "savedata", TitleId);

            background = LoadImage("background.png");
            playImage = LoadImage("btn.png");
            padImage = LoadImage("ps4_pad.png");

            DefineActions();
            LoadBindings();
            LoadConfig();
            Loc.Lang = Convert.ToInt32(LauncherValue("language", 1)); // English until another language is saved

            try
            {
                if (File.Exists(gameExe))
                {
                    var fvi = FileVersionInfo.GetVersionInfo(gameExe);
                    if (fvi.FileMajorPart + fvi.FileMinorPart + fvi.FileBuildPart > 0)
                        PortVersion = fvi.FileMajorPart + "." + fvi.FileMinorPart + "." + fvi.FileBuildPart;
                }
            }
            catch (Exception) { }
            Text = "NHL24 PC v" + PortVersion;
            ClientSize = new Size(1180, 885);
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Color.FromArgb(10, 16, 26);
            KeyPreview = true;
            KeyDown += OnKeyDown;
            SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint, true);
            DoubleBuffered = true;
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch (Exception) { }

            BuildUi();
            if (startAdvanced) ShowAdvanced(); else ShowSimple();
            RefreshReadyCheck();
            RefreshBindList();
        }

        Image LoadImage(string name)
        {
            var path = FindFile(name);
            if (path == null) return null;
            try { return Image.FromFile(path); } catch (Exception) { return null; }
        }

        string FindFile(string name)
        {
            string[] candidates = {
                Path.Combine(baseDir, name),
                Path.Combine(baseDir, "..", name),
                Path.Combine(baseDir, "..", "..", name),
            };
            foreach (var c in candidates)
                if (File.Exists(c)) return Path.GetFullPath(c);
            return null;
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            if (background == null)
            {
                base.OnPaintBackground(e);
                return;
            }
            // cover: keep the aspect ratio, crop the overflow
            float scale = Math.Max((float)ClientSize.Width / background.Width, (float)ClientSize.Height / background.Height);
            int w = (int)(background.Width * scale), h = (int)(background.Height * scale);
            e.Graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
            e.Graphics.DrawImage(background, (ClientSize.Width - w) / 2, (ClientSize.Height - h) / 2, w, h);
        }

        // ------------------------------------------------------------------ game folder / sfo
        string GameFolder
        {
            get
            {
                if (gameFolderBox != null && gameFolderBox.Text.Trim().Length > 0) return gameFolderBox.Text.Trim();
                return DefaultGameFolder();
            }
        }

        string DefaultGameFolder()
        {
            try
            {
                if (File.Exists(gamePathFile))
                {
                    var line = File.ReadAllLines(gamePathFile)[0].Trim();
                    if (line.Length > 0) return Path.GetDirectoryName(line);
                }
            }
            catch (Exception) { }
            string[] candidates = {
                Path.Combine(baseDir, "game"),
                Path.Combine(baseDir, "..", "GAME_FILE", "uroot"),
                Path.Combine(baseDir, "..", "..", "GAME_FILE", "uroot"),
            };
            foreach (var c in candidates)
                if (File.Exists(Path.Combine(c, "eboot.bin"))) return Path.GetFullPath(c);
            return Path.Combine(baseDir, "game");
        }

        static string ReadSfoString(string sfoPath, string wantedKey)
        {
            try
            {
                var d = File.ReadAllBytes(sfoPath);
                if (d.Length < 20 || BitConverter.ToUInt32(d, 0) != 0x46535000) return null;
                uint keyTable = BitConverter.ToUInt32(d, 8), dataTable = BitConverter.ToUInt32(d, 12), count = BitConverter.ToUInt32(d, 16);
                for (int i = 0; i < count; i++)
                {
                    int e = 20 + i * 16;
                    ushort keyOff = BitConverter.ToUInt16(d, e);
                    ushort fmt = BitConverter.ToUInt16(d, e + 2);
                    uint len = BitConverter.ToUInt32(d, e + 4);
                    uint dataOff = BitConverter.ToUInt32(d, e + 12);
                    int k = (int)(keyTable + keyOff), kend = k;
                    while (kend < d.Length && d[kend] != 0) kend++;
                    var key = Encoding.ASCII.GetString(d, k, kend - k);
                    if (key != wantedKey) continue;
                    if (fmt == 0x0404) return BitConverter.ToUInt32(d, (int)(dataTable + dataOff)).ToString();
                    int s = (int)(dataTable + dataOff), send = s;
                    while (send < s + len && send < d.Length && d[send] != 0) send++;
                    return Encoding.UTF8.GetString(d, s, send - s);
                }
            }
            catch (Exception) { }
            return null;
        }

        string InstalledVersion(string folder)
        {
            if (!File.Exists(Path.Combine(folder, "eboot.bin"))) return null;
            return ReadSfoString(Path.Combine(folder, "sce_sys", "param.sfo"), "APP_VER") ?? "?";
        }

        // ------------------------------------------------------------------ ready check
        void RefreshReadyCheck()
        {
            var folder = GameFolder;
            var version = InstalledVersion(folder);
            if (version != null)
            {
                var text = Loc.F("game_ok", version, folder);
                var upd = InstalledVersion(folder + "-UPDATE");
                if (upd != null) text += Loc.F("game_upd", upd);
                rowGame.Text = text; rowGame.Ok = true;
                gameVersionLabel.Text = Loc.F("game_line", upd ?? version);
            }
            else
            {
                rowGame.Text = Loc.T("game_missing"); rowGame.Ok = false;
                gameVersionLabel.Text = Loc.T("game_line_none");
            }
            rowGame.Invalidate();

            int profiles = 0;
            try { if (Directory.Exists(savesDir)) profiles = Directory.GetDirectories(savesDir).Length; } catch (Exception) { }
            var shortSaves = Path.Combine("user", "home", "1000", "savedata", TitleId);
            if (profiles > 0) { rowSaves.Text = Loc.F("saves_ok", profiles, shortSaves); rowSaves.Ok = true; }
            else { rowSaves.Text = Loc.F("saves_none", shortSaves); rowSaves.Ok = false; }
            rowSaves.Invalidate();

            var gpu = DetectGpu();
            if (gpu != null) { rowGpu.Text = Loc.F("gpu", gpu); rowGpu.Ok = true; }
            else { rowGpu.Text = Loc.T("gpu_unknown"); rowGpu.Ok = false; }
            rowGpu.Invalidate();

            bool custom = false;
            foreach (var a in actions) if (bindings[a.Id] != a.Default) custom = true;
            rowControls.Text = Loc.T(custom ? "controls_custom" : "controls_default"); rowControls.Ok = true;
            rowControls.Invalidate();

            pkgReqLabel.Text = Loc.F("pkg_req", Path.GetFileName(folder.TrimEnd('\\', '/')));
            RefreshSummary();
        }

        static string DetectGpu()
        {
            try
            {
                using (var searcher = new ManagementObjectSearcher("SELECT Name, AdapterRAM, PNPDeviceID FROM Win32_VideoController"))
                {
                    string best = null; long bestRam = -1;
                    foreach (ManagementObject mo in searcher.Get())
                    {
                        var name = Convert.ToString(mo["Name"]);
                        long ram = 0;
                        try { ram = Convert.ToInt64(mo["AdapterRAM"]); } catch (Exception) { }
                        long qw = RegistryVram(name);
                        if (qw > 0) ram = qw;
                        if (ram > bestRam) { bestRam = ram; best = name; }
                    }
                    if (best == null) return null;
                    return bestRam > 0 ? best + ", " + (bestRam / (1024 * 1024)) + " MiB" : best;
                }
            }
            catch (Exception) { return null; }
        }

        // AdapterRAM is a 32-bit value; the display class registry keeps the real size.
        static long RegistryVram(string name)
        {
            try
            {
                using (var cls = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"))
                {
                    if (cls == null) return 0;
                    foreach (var sub in cls.GetSubKeyNames())
                    {
                        using (var k = cls.OpenSubKey(sub))
                        {
                            if (k == null) continue;
                            var desc = Convert.ToString(k.GetValue("DriverDesc"));
                            if (desc != name) continue;
                            var v = k.GetValue("HardwareInformation.qwMemorySize");
                            if (v != null) return Convert.ToInt64(v);
                        }
                    }
                }
            }
            catch (Exception) { }
            return 0;
        }

        // ------------------------------------------------------------------ actions / bindings (ini)
        void DefineActions()
        {
            // Marker coordinates refer to the 1254x1254 ps4_pad.png.
            actions.Add(new PadAction("cross", "Kríž (X)", "n", 1030, 551));
            actions.Add(new PadAction("circle", "Kruh (O)", "b", 1120, 457));
            actions.Add(new PadAction("square", "Štvorec", "v", 936, 457));
            actions.Add(new PadAction("triangle", "Trojuholník", "c", 1030, 367));
            actions.Add(new PadAction("l1", "L1", "q", 237, 251));
            actions.Add(new PadAction("r1", "R1", "u", 1012, 251));
            actions.Add(new PadAction("l2", "L2", "e", 237, 205));
            actions.Add(new PadAction("r2", "R2", "o", 1012, 205));
            actions.Add(new PadAction("l3", "L3 (stlačenie ľavej páčky)", "x", 421, 627));
            actions.Add(new PadAction("r3", "R3 (stlačenie pravej páčky)", "m", 833, 627));
            actions.Add(new PadAction("options", "OPTIONS", "enter", 882, 331));
            actions.Add(new PadAction("back", "SHARE", "backspace", 372, 331));
            actions.Add(new PadAction("touchpad_center", "Touchpad", "space", 627, 394));
            actions.Add(new PadAction("pad_up", "D-pad hore", "up", 228, 331));
            actions.Add(new PadAction("pad_down", "D-pad dole", "down", 228, 582));
            actions.Add(new PadAction("pad_left", "D-pad vľavo", "left", 103, 457));
            actions.Add(new PadAction("pad_right", "D-pad vpravo", "right", 354, 457));
            actions.Add(new PadAction("axis_left_y_minus", "Ľavá páčka hore", "w", 421, 560));
            actions.Add(new PadAction("axis_left_y_plus", "Ľavá páčka dole", "s", 421, 694));
            actions.Add(new PadAction("axis_left_x_minus", "Ľavá páčka vľavo", "a", 354, 627));
            actions.Add(new PadAction("axis_left_x_plus", "Ľavá páčka vpravo", "d", 488, 627));
            actions.Add(new PadAction("axis_right_y_minus", "Pravá páčka hore", "i", 833, 560));
            actions.Add(new PadAction("axis_right_y_plus", "Pravá páčka dole", "k", 833, 694));
            actions.Add(new PadAction("axis_right_x_minus", "Pravá páčka vľavo", "j", 766, 627));
            actions.Add(new PadAction("axis_right_x_plus", "Pravá páčka vpravo", "l", 900, 627));
        }

        void LoadBindings()
        {
            foreach (var a in actions) bindings[a.Id] = a.Default;
            if (!File.Exists(inputPath)) return;
            bool keyboardSection = false;
            foreach (var raw in File.ReadAllLines(inputPath))
            {
                var line = raw.Trim();
                if (line.StartsWith("# Keyboard bindings")) { keyboardSection = true; continue; }
                if (line.StartsWith("# Controller bindings")) { keyboardSection = false; continue; }
                if (!keyboardSection || line.Length == 0 || line.StartsWith("#")) continue;
                var eq = line.IndexOf('=');
                if (eq < 0) continue;
                var key = line.Substring(0, eq).Trim();
                var value = line.Substring(eq + 1).Trim();
                if (bindings.ContainsKey(key)) bindings[key] = value; // the last one wins
            }
        }

        void SaveBindings()
        {
            var sb = new StringBuilder();
            sb.AppendLine("# NHL24 PC – ovládanie (generované launcherom)");
            sb.AppendLine();
            sb.AppendLine("# Keyboard bindings");
            sb.AppendLine();
            foreach (var a in actions)
                sb.AppendLine(a.Id + " = " + bindings[a.Id]);
            sb.AppendLine();
            sb.AppendLine("# Controller bindings");
            sb.AppendLine();
            string[] pad = { "triangle", "cross", "square", "circle", "l1", "l2", "l3", "r1", "r2", "r3",
                             "options", "pad_up", "pad_down", "pad_left", "pad_right",
                             "axis_left_x", "axis_left_y", "axis_right_x", "axis_right_y" };
            foreach (var p in pad) sb.AppendLine(p + " = " + p);
            sb.AppendLine("touchpad_center = back");
            sb.AppendLine();
            sb.AppendLine("# Range of deadzones: 1 (almost none) to 127 (max)");
            sb.AppendLine("analog_deadzone = leftjoystick, 5, 127");
            sb.AppendLine("analog_deadzone = rightjoystick, 5, 127");
            sb.AppendLine();
            sb.AppendLine("override_controller_color = false, 0, 0, 255");
            Directory.CreateDirectory(Path.GetDirectoryName(inputPath));
            File.WriteAllText(inputPath, sb.ToString(), new UTF8Encoding(false));
        }

        // ------------------------------------------------------------------ config.json
        void LoadConfig()
        {
            config = null;
            if (File.Exists(configPath))
            {
                try
                {
                    var ser = new JavaScriptSerializer();
                    config = ser.Deserialize<Dictionary<string, object>>(File.ReadAllText(configPath));
                }
                catch (Exception) { config = null; }
            }
            if (config == null) config = new Dictionary<string, object>();
            if (!config.ContainsKey("GPU") || !(config["GPU"] is Dictionary<string, object>))
                config["GPU"] = new Dictionary<string, object>();
            if (!config.ContainsKey("Launcher") || !(config["Launcher"] is Dictionary<string, object>))
                config["Launcher"] = new Dictionary<string, object>();
            if (!config.ContainsKey("General") || !(config["General"] is Dictionary<string, object>))
                config["General"] = new Dictionary<string, object>();
        }

        Dictionary<string, object> Gpu { get { return (Dictionary<string, object>)config["GPU"]; } }
        Dictionary<string, object> Launcher { get { return (Dictionary<string, object>)config["Launcher"]; } }
        Dictionary<string, object> General { get { return (Dictionary<string, object>)config["General"]; } }

        object GpuValue(string key, object fallback)
        {
            object v;
            return Gpu.TryGetValue(key, out v) ? v : fallback;
        }

        object LauncherValue(string key, object fallback)
        {
            object v;
            return Launcher.TryGetValue(key, out v) ? v : fallback;
        }

        // General section of the emulator: only redzone_patches is edited, the other keys are kept as they are
        object GeneralValue(string key, object fallback)
        {
            object v;
            return General.TryGetValue(key, out v) ? v : fallback;
        }

        static bool ToBool(object value, bool fallback)
        {
            if (value == null) return fallback;
            if (value is bool) return (bool)value;
            bool b;
            if (bool.TryParse(Convert.ToString(value), out b)) return b;
            return fallback;
        }

        void SaveConfig()
        {
            // the simple view and the advanced tab edit the same values; the one shown last wins
            var parts = resolutionBox.Text.Split('x');
            int w = 1280, h = 720;
            if (parts.Length == 2) { int.TryParse(parts[0].Trim(), out w); int.TryParse(parts[1].Trim(), out h); }
            w = Math.Max(640, Math.Min(7680, w));
            h = Math.Max(360, Math.Min(4320, h));
            Gpu["window_width"] = w;
            Gpu["window_height"] = h;
            // The game renders at its native 1920x1080; the emulator reports a 1080p screen so
            // the game lays out its frame correctly, and scales the output to the window (FSR).
            Gpu["internal_screen_width"] = 1920;
            Gpu["internal_screen_height"] = 1080;
            Gpu["full_screen"] = fullscreenBox.Checked;
            Gpu["full_screen_mode"] = fullscreenModeBox.SelectedIndex == 1 ? "Fullscreen" : "Windowed";
            Gpu["present_mode"] = vsyncBox.Checked ? "Fifo" : "Mailbox";
            Gpu["fsr_enabled"] = fsrBox.Checked;
            Gpu["rcas_enabled"] = rcasBox.Checked;
            Gpu["rcas_attenuation"] = sharpnessBar.Value;
            Gpu["vblank_frequency"] = 60;
            Launcher["language"] = Loc.Lang;
            // Windows static guest red-zone protection of the emulator (src/core/cpu_patches.cpp): fixes the
            // crash while a match loads when a page-protection fault clobbers the red zone of a leaf function.
            General["redzone_patches"] = redzoneQuick.Checked;
            var ser = new JavaScriptSerializer();
            Directory.CreateDirectory(Path.GetDirectoryName(configPath));
            File.WriteAllText(configPath, PrettyJson(ser.Serialize(config)), new UTF8Encoding(false));

            // game folder: only a folder other than the default lookup needs user\game_path.txt
            var folder = gameFolderBox.Text.Trim();
            bool isDefault = folder.Length == 0;
            string[] defaults = { Path.Combine(baseDir, "game"), Path.Combine(baseDir, "..", "GAME_FILE", "uroot"), Path.Combine(baseDir, "..", "..", "GAME_FILE", "uroot") };
            foreach (var d in defaults)
                if (!isDefault && string.Equals(Path.GetFullPath(folder), Path.GetFullPath(d), StringComparison.OrdinalIgnoreCase)) isDefault = true;
            if (isDefault)
            {
                if (File.Exists(gamePathFile)) File.Delete(gamePathFile);
            }
            else
            {
                Directory.CreateDirectory(Path.GetDirectoryName(gamePathFile));
                File.WriteAllText(gamePathFile, Path.Combine(folder, "eboot.bin") + "\r\n", new UTF8Encoding(false));
            }
        }

        // JavaScriptSerializer writes one line; indent it so the file stays readable.
        static string PrettyJson(string json)
        {
            var sb = new StringBuilder();
            int indent = 0;
            bool inString = false;
            for (int i = 0; i < json.Length; i++)
            {
                char c = json[i];
                if (c == '"' && (i == 0 || json[i - 1] != '\\')) inString = !inString;
                if (inString) { sb.Append(c); continue; }
                switch (c)
                {
                    case '{':
                    case '[':
                        sb.Append(c); sb.AppendLine(); indent++; sb.Append(new string(' ', indent * 2)); break;
                    case '}':
                    case ']':
                        sb.AppendLine(); indent--; sb.Append(new string(' ', indent * 2)); sb.Append(c); break;
                    case ',':
                        sb.Append(c); sb.AppendLine(); sb.Append(new string(' ', indent * 2)); break;
                    case ':':
                        sb.Append(": "); break;
                    default:
                        sb.Append(c); break;
                }
            }
            return sb.ToString();
        }

        // ------------------------------------------------------------------ UI
        void BuildUi()
        {
            SuspendLayout();

            // header texts under the logo (the logo is part of the background picture)
            versionLabel = new TransparentLabel { Left = 46, Top = 172, Width = 400, Height = 22, Font = Theme.Body, Text = Loc.F("version_line", PortVersion) };
            gameVersionLabel = new TransparentLabel { Left = 46, Top = 194, Width = 500, Height = 22, Font = Theme.Body };
            Controls.Add(versionLabel);
            Controls.Add(gameVersionLabel);

            // view switch
            simpleBtn = new FlatButton { Left = 962, Top = 30, Width = 100, Height = 34, Text = Loc.T("simple") };
            advancedBtn = new FlatButton { Left = 1062, Top = 30, Width = 100, Height = 34, Text = Loc.T("advanced") };
            simpleBtn.Click += delegate { ShowSimple(); };
            advancedBtn.Click += delegate { ShowAdvanced(); };
            Controls.Add(simpleBtn);
            Controls.Add(advancedBtn);

            BuildSimpleView();
            BuildAdvancedView();
            BuildBottomBar();
            BuildProgressPanel();

            ResumeLayout();
        }

        void BuildSimpleView()
        {
            simpleView = new Panel { Left = 0, Top = 236, Width = 1180, Height = 520, BackColor = Color.Transparent };
            Controls.Add(simpleView);

            // ---- ready check
            readyPanel = new GlassPanel { Left = 30, Top = 0, Width = 440, Height = 515 };
            simpleView.Controls.Add(readyPanel);
            readyPanel.Controls.Add(new SectionTitle { Left = 16, Top = 18, Width = 400, Text = Loc.T("ready") });
            rowGame = new CheckRow { Left = 24, Top = 84, Width = 396, Font = Theme.Body };
            rowSaves = new CheckRow { Left = 24, Top = 140, Width = 396, Font = Theme.Body };
            rowGpu = new CheckRow { Left = 24, Top = 196, Width = 396, Font = Theme.Body };
            rowControls = new CheckRow { Left = 24, Top = 252, Width = 396, Font = Theme.Body };
            readyPanel.Controls.Add(rowGame); readyPanel.Controls.Add(rowSaves);
            readyPanel.Controls.Add(rowGpu); readyPanel.Controls.Add(rowControls);
            var openBtn = new FlatButton { Left = 28, Top = 318, Width = 160, Height = 36, Text = Loc.T("open_folder") };
            openBtn.Click += delegate { OpenFolder(); };
            readyPanel.Controls.Add(openBtn);
            readyPanel.Controls.Add(new TransparentLabel { Left = 28, Top = 380, Width = 390, Height = 60, Font = Theme.Small, ForeColor = Color.FromArgb(200, 210, 225), Text = Loc.T("hint") });

            // ---- quick settings
            quickPanel = new GlassPanel { Left = 490, Top = 0, Width = 660, Height = 515 };
            simpleView.Controls.Add(quickPanel);
            quickPanel.Controls.Add(new SectionTitle { Left = 16, Top = 18, Width = 600, Text = Loc.T("quick") });

            int y = 82;
            quickPanel.Controls.Add(new TransparentLabel { Left = 28, Top = y, Width = 300, Height = 20, Font = Theme.Label, Text = Loc.T("game_folder") });
            y += 22;
            gameFolderBox = new TextBox { Left = 28, Top = y, Width = 480, Height = 30 };
            Theme.Style(gameFolderBox);
            gameFolderBox.Text = DefaultGameFolder();
            gameFolderBox.TextChanged += delegate { RefreshReadyCheck(); };
            quickPanel.Controls.Add(gameFolderBox);
            var browseBtn = new FlatButton { Left = 518, Top = y - 2, Width = 112, Height = 32, Text = Loc.T("browse") };
            browseBtn.Click += delegate { BrowseGameFolder(); };
            quickPanel.Controls.Add(browseBtn);
            y += 40;
            var installBtn = new FlatButton { Left = 28, Top = y, Width = 170, Height = 34, Text = Loc.T("install_pkg") };
            installBtn.Click += delegate { InstallPkg(); };
            quickPanel.Controls.Add(installBtn);
            pkgReqLabel = new TransparentLabel { Left = 208, Top = y - 2, Width = 430, Height = 40, Font = Theme.Small, ForeColor = Color.FromArgb(200, 210, 225) };
            quickPanel.Controls.Add(pkgReqLabel);
            y += 52;

            // two columns
            int c1 = 28, c2 = 344, cw = 290;
            quickPanel.Controls.Add(new TransparentLabel { Left = c1, Top = y, Width = cw, Height = 20, Font = Theme.Label, Text = Loc.T("output") });
            quickPanel.Controls.Add(new TransparentLabel { Left = c2, Top = y, Width = cw, Height = 20, Font = Theme.Label, Text = Loc.T("display_mode") });
            y += 22;
            outputBox = new ComboBox { Left = c1, Top = y, Width = cw, DropDownStyle = ComboBoxStyle.DropDown };
            Theme.Style(outputBox);
            outputBox.Items.AddRange(new object[] { "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3440 x 1440", "3840 x 2160" });
            outputBox.Text = GpuValue("window_width", 1280) + " x " + GpuValue("window_height", 720);
            quickPanel.Controls.Add(outputBox);
            displayModeBox = new ComboBox { Left = c2, Top = y, Width = cw, DropDownStyle = ComboBoxStyle.DropDownList };
            Theme.Style(displayModeBox);
            displayModeBox.Items.AddRange(new object[] { Loc.T("windowed"), Loc.T("borderless"), Loc.T("exclusive") });
            bool fs = Convert.ToBoolean(GpuValue("full_screen", false));
            displayModeBox.SelectedIndex = !fs ? 0 : (Convert.ToString(GpuValue("full_screen_mode", "Windowed")) == "Fullscreen" ? 2 : 1);
            quickPanel.Controls.Add(displayModeBox);
            y += 44;
            quickPanel.Controls.Add(new TransparentLabel { Left = c1, Top = y, Width = cw, Height = 20, Font = Theme.Label, Text = Loc.T("upscaler") });
            quickPanel.Controls.Add(new TransparentLabel { Left = c2, Top = y, Width = cw, Height = 20, Font = Theme.Label, Text = Loc.T("sharpness") });
            y += 22;
            upscalerBox = new ComboBox { Left = c1, Top = y, Width = cw, DropDownStyle = ComboBoxStyle.DropDownList };
            Theme.Style(upscalerBox);
            upscalerBox.Items.AddRange(new object[] { Loc.T("ups_off"), Loc.T("ups_fsr"), Loc.T("ups_fsr_rcas") });
            bool fsr = Convert.ToBoolean(GpuValue("fsr_enabled", false)), rcas = Convert.ToBoolean(GpuValue("rcas_enabled", true));
            upscalerBox.SelectedIndex = !fsr ? 0 : (rcas ? 2 : 1);
            quickPanel.Controls.Add(upscalerBox);
            sharpBar = new GlassSlider { Left = c2 - 8, Top = y - 1, Width = cw - 60, Minimum = 0, Maximum = 1000, Value = Math.Max(0, Math.Min(1000, Convert.ToInt32(GpuValue("rcas_attenuation", 250)))) };
            quickPanel.Controls.Add(sharpBar);
            sharpValue = new TransparentLabel { Left = c2 + cw - 60, Top = y + 4, Width = 60, Height = 20, Font = Theme.Label, Text = sharpBar.Value.ToString() };
            sharpBar.ValueChanged += delegate { sharpValue.Text = sharpBar.Value.ToString(); };
            sharpBar.Enabled = upscalerBox.SelectedIndex == 2;
            quickPanel.Controls.Add(sharpValue);
            y += 52;
            quickPanel.Controls.Add(new TransparentLabel { Left = c1, Top = y, Width = cw, Height = 20, Font = Theme.Label, Text = Loc.T("lang") });
            y += 22;
            langBox = new ComboBox { Left = c1, Top = y, Width = cw, DropDownStyle = ComboBoxStyle.DropDownList };
            Theme.Style(langBox);
            langBox.Items.AddRange(new object[] { "Slovenčina", "English" });
            langBox.SelectedIndex = Loc.Lang;
            langBox.SelectedIndexChanged += delegate { if (langBox.SelectedIndex != Loc.Lang) { Loc.Lang = langBox.SelectedIndex; Relayout(); } };
            quickPanel.Controls.Add(langBox);
            vsyncQuick = new CheckBox { Left = c2, Top = y + 2, Width = cw, Height = 24, Font = Theme.Body, ForeColor = Color.White, BackColor = Color.Transparent, Text = Loc.T("vsync"),
                Checked = Convert.ToString(GpuValue("present_mode", "Mailbox")) == "Fifo" };
            quickPanel.Controls.Add(vsyncQuick);
            quickPanel.Controls.Add(new TransparentLabel { Left = c2, Top = y + 28, Width = cw, Height = 40, Font = Theme.Small, ForeColor = Color.FromArgb(200, 210, 225), Text = Loc.T("vsync_desc") });
            y += 70;
            redzoneQuick = new CheckBox { Left = c1, Top = y, Width = 600, Height = 24, Font = Theme.Body, ForeColor = Color.White, BackColor = Color.Transparent, Text = Loc.T("redzone"),
                Checked = ToBool(GeneralValue("redzone_patches", false), false) };
            quickPanel.Controls.Add(redzoneQuick);
            quickPanel.Controls.Add(new TransparentLabel { Left = c1, Top = y + 26, Width = 600, Height = 44, Font = Theme.Small, ForeColor = Color.FromArgb(200, 210, 225), Text = Loc.T("redzone_desc") });
            redzoneQuick.CheckedChanged += delegate { RefreshSummary(); };

            // the quick controls mirror the advanced ones
            outputBox.TextChanged += delegate { resolutionBox.Text = outputBox.Text; RefreshSummary(); };
            displayModeBox.SelectedIndexChanged += delegate
            {
                fullscreenBox.Checked = displayModeBox.SelectedIndex != 0;
                fullscreenModeBox.SelectedIndex = displayModeBox.SelectedIndex == 2 ? 1 : 0;
                RefreshSummary();
            };
            upscalerBox.SelectedIndexChanged += delegate
            {
                fsrBox.Checked = upscalerBox.SelectedIndex != 0;
                rcasBox.Checked = upscalerBox.SelectedIndex == 2;
                sharpBar.Enabled = upscalerBox.SelectedIndex == 2;
                RefreshSummary();
            };
            sharpBar.ValueChanged += delegate { sharpnessBar.Value = sharpBar.Value; };
            vsyncQuick.CheckedChanged += delegate { vsyncBox.Checked = vsyncQuick.Checked; RefreshSummary(); };
        }

        void BuildAdvancedView()
        {
            advancedView = new GlassPanel { Left = 30, Top = 236, Width = 1120, Height = 515, Visible = false };
            Controls.Add(advancedView);

            var tabs = new TabControl { Left = 12, Top = 12, Width = 1096, Height = 491 };
            var controlsTab = new TabPage(Loc.T("tab_controls")) { BackColor = Color.FromArgb(24, 31, 42) };
            var displayTab = new TabPage(Loc.T("tab_display")) { BackColor = Color.FromArgb(24, 31, 42) };
            tabs.TabPages.Add(controlsTab);
            tabs.TabPages.Add(displayTab);

            // ---- controls tab
            padBox = new PictureBox { Left = 10, Top = 10, Width = 440, Height = 440, SizeMode = PictureBoxSizeMode.Zoom, BackColor = Color.FromArgb(24, 31, 42) };
            if (padImage != null) padBox.Image = padImage;
            padBox.Paint += PadPaint;
            padBox.MouseClick += PadClick;
            controlsTab.Controls.Add(padBox);

            var hint = new Label { Left = 460, Top = 10, Width = 610, Height = 44, ForeColor = Color.FromArgb(200, 210, 225), BackColor = Color.Transparent, Font = Theme.Small, Text = Loc.T("pad_hint") };
            controlsTab.Controls.Add(hint);

            bindList = new ListView { Left = 460, Top = 58, Width = 610, Height = 340, View = View.Details, FullRowSelect = true, HideSelection = false,
                BackColor = Theme.Field, ForeColor = Color.White, BorderStyle = BorderStyle.FixedSingle, Font = Theme.Label };
            bindList.Columns.Add(Loc.T("col_action"), 330);
            bindList.Columns.Add(Loc.T("col_key"), 240);
            bindList.DoubleClick += delegate { if (bindList.SelectedItems.Count > 0) BeginCapture((PadAction)bindList.SelectedItems[0].Tag); };
            controlsTab.Controls.Add(bindList);

            captureLabel = new Label { Left = 460, Top = 404, Width = 610, Height = 24, Font = new Font("Segoe UI", 10f, FontStyle.Bold), ForeColor = Theme.Orange, BackColor = Color.Transparent, Text = "" };
            controlsTab.Controls.Add(captureLabel);

            var defaultsBtn = new FlatButton { Left = 460, Top = 428, Width = 180, Height = 30, Text = Loc.T("defaults") };
            defaultsBtn.Click += delegate { foreach (var a in actions) bindings[a.Id] = a.Default; RefreshBindList(); padBox.Invalidate(); RefreshReadyCheck(); };
            controlsTab.Controls.Add(defaultsBtn);

            // ---- display tab (the full set; mirrors the quick settings)
            int y = 20;
            displayTab.Controls.Add(new Label { Left = 20, Top = y, Width = 300, ForeColor = Color.White, BackColor = Color.Transparent, Font = Theme.Body, Text = Loc.T("resolution") });
            resolutionBox = new ComboBox { Left = 330, Top = y - 3, Width = 200, DropDownStyle = ComboBoxStyle.DropDown };
            Theme.Style(resolutionBox);
            resolutionBox.Items.AddRange(new object[] { "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3440 x 1440", "3840 x 2160" });
            resolutionBox.Text = outputBox.Text;
            displayTab.Controls.Add(resolutionBox);
            y += 40;
            fullscreenBox = new CheckBox { Left = 20, Top = y, Width = 300, ForeColor = Color.White, BackColor = Color.Transparent, Font = Theme.Body, Text = Loc.T("borderless").Split('(')[0].Trim(), Checked = displayModeBox.SelectedIndex != 0 };
            displayTab.Controls.Add(fullscreenBox);
            fullscreenModeBox = new ComboBox { Left = 330, Top = y - 3, Width = 260, DropDownStyle = ComboBoxStyle.DropDownList };
            Theme.Style(fullscreenModeBox);
            fullscreenModeBox.Items.AddRange(new object[] { Loc.T("borderless"), Loc.T("exclusive") });
            fullscreenModeBox.SelectedIndex = displayModeBox.SelectedIndex == 2 ? 1 : 0;
            displayTab.Controls.Add(fullscreenModeBox);
            y += 40;
            vsyncBox = new CheckBox { Left = 20, Top = y, Width = 500, ForeColor = Color.White, BackColor = Color.Transparent, Font = Theme.Body, Text = Loc.T("vsync") + " – " + Loc.T("vsync_desc"), Checked = vsyncQuick.Checked };
            displayTab.Controls.Add(vsyncBox);
            y += 50;
            var fsrGroup = new GroupBox { Left = 20, Top = y, Width = 640, Height = 150, ForeColor = Color.White, Font = Theme.Body, Text = Loc.T("fsr_group") };
            fsrBox = new CheckBox { Left = 15, Top = 25, Width = 400, ForeColor = Color.White, Text = Loc.T("fsr_box"), Checked = upscalerBox.SelectedIndex != 0 };
            rcasBox = new CheckBox { Left = 15, Top = 55, Width = 400, ForeColor = Color.White, Text = Loc.T("rcas_box"), Checked = upscalerBox.SelectedIndex == 2 };
            sharpnessBar = new TrackBar { Left = 15, Top = 85, Width = 400, Minimum = 0, Maximum = 1000, TickFrequency = 100, Value = sharpBar.Value, BackColor = Color.FromArgb(24, 31, 42) };
            sharpnessLabel = new Label { Left = 430, Top = 90, Width = 180, ForeColor = Color.White };
            sharpnessBar.ValueChanged += delegate { sharpnessLabel.Text = Loc.T("sharpness") + ": " + sharpnessBar.Value; };
            sharpnessLabel.Text = Loc.T("sharpness") + ": " + sharpnessBar.Value;
            fsrGroup.Controls.Add(fsrBox); fsrGroup.Controls.Add(rcasBox); fsrGroup.Controls.Add(sharpnessBar); fsrGroup.Controls.Add(sharpnessLabel);
            displayTab.Controls.Add(fsrGroup);
            y += 170;
            displayTab.Controls.Add(new Label { Left = 20, Top = y, Width = 1000, Height = 80, ForeColor = Color.FromArgb(200, 210, 225), BackColor = Color.Transparent, Font = Theme.Small, Text = Loc.T("display_note") });

            // advanced -> quick mirroring
            resolutionBox.TextChanged += delegate { if (outputBox.Text != resolutionBox.Text) outputBox.Text = resolutionBox.Text; };
            EventHandler syncMode = delegate
            {
                int idx = !fullscreenBox.Checked ? 0 : (fullscreenModeBox.SelectedIndex == 1 ? 2 : 1);
                if (displayModeBox.SelectedIndex != idx) displayModeBox.SelectedIndex = idx;
            };
            fullscreenBox.CheckedChanged += syncMode;
            fullscreenModeBox.SelectedIndexChanged += syncMode;
            EventHandler syncFsr = delegate
            {
                int idx = !fsrBox.Checked ? 0 : (rcasBox.Checked ? 2 : 1);
                if (upscalerBox.SelectedIndex != idx) upscalerBox.SelectedIndex = idx;
            };
            fsrBox.CheckedChanged += syncFsr;
            rcasBox.CheckedChanged += syncFsr;
            sharpnessBar.ValueChanged += delegate { if (sharpBar.Value != sharpnessBar.Value) sharpBar.Value = sharpnessBar.Value; };
            vsyncBox.CheckedChanged += delegate { if (vsyncQuick.Checked != vsyncBox.Checked) vsyncQuick.Checked = vsyncBox.Checked; };

            advancedView.Controls.Add(tabs);
        }

        void BuildBottomBar()
        {
            var bar = new GlassPanel { Left = 0, Top = 770, Width = 1180, Height = 115, Radius = 1, Fill = Color.FromArgb(215, 6, 10, 18), Border = Color.FromArgb(80, 255, 255, 255) };
            Controls.Add(bar);
            summaryLabel = new TransparentLabel { Left = 46, Top = 40, Width = 640, Height = 26, Font = new Font("Segoe UI", 11f, FontStyle.Bold), ForeColor = Color.FromArgb(230, 235, 245) };
            bar.Controls.Add(summaryLabel);
            statusLabel = new TransparentLabel { Left = 46, Top = 72, Width = 660, Height = 22, AutoEllipsis = true, Font = Theme.Small, ForeColor = Color.FromArgb(180, 190, 205) };
            bar.Controls.Add(statusLabel);
            var saveBtn = new FlatButton { Left = 720, Top = 40, Width = 110, Height = 36, Text = Loc.T("save") };
            saveBtn.Click += delegate { if (SaveAll()) statusLabel.Text = Loc.T("saved") + ": " + configPath; };
            bar.Controls.Add(saveBtn);
            playButton = new ImageButton(playImage) { Left = 850, Top = 8, Width = 310, Height = 100, Text = Loc.T("play") };
            playButton.Click += delegate { if (SaveAll()) RunGame(); };
            bar.Controls.Add(playButton);
            statusLabel.Text = File.Exists(gameExe) ? "nhl24.exe: " + gameExe : Loc.F("no_exe", gameExe);
        }

        void BuildProgressPanel()
        {
            progressPanel = new GlassPanel { Left = 290, Top = 330, Width = 600, Height = 230, Visible = false, Fill = Color.FromArgb(240, 8, 12, 20), Border = Theme.Orange };
            Controls.Add(progressPanel);
            progressPanel.BringToFront();
            progressPanel.Controls.Add(new SectionTitle { Left = 16, Top = 14, Width = 560, Text = Loc.T("pkg_title") });
            progressTitle = new TransparentLabel { Left = 28, Top = 72, Width = 544, Height = 24, Font = Theme.Body };
            progressFile = new TransparentLabel { Left = 28, Top = 98, Width = 544, Height = 22, Font = Theme.Small, ForeColor = Color.FromArgb(200, 210, 225), AutoEllipsis = true };
            progressBar = new ProgressBar { Left = 28, Top = 126, Width = 544, Height = 22, Minimum = 0, Maximum = 1000 };
            progressNumbers = new TransparentLabel { Left = 28, Top = 154, Width = 544, Height = 22, Font = Theme.Small };
            progressCancel = new FlatButton { Left = 452, Top = 184, Width = 120, Height = 32, Text = Loc.T("cancel") };
            progressCancel.Click += delegate { CancelOrClosePkg(); };
            progressPanel.Controls.Add(progressTitle); progressPanel.Controls.Add(progressFile);
            progressPanel.Controls.Add(progressBar); progressPanel.Controls.Add(progressNumbers);
            progressPanel.Controls.Add(progressCancel);
        }

        void Relayout()
        {
            // language change: rebuild everything with the new strings
            var state = new Dictionary<string, object>();
            SaveAllSilently();
            Controls.Clear();
            BuildUi();
            ShowSimple();
            RefreshReadyCheck();
            RefreshBindList();
        }

        void ShowSimple()
        {
            simpleView.Visible = true;
            advancedView.Visible = false;
            simpleBtn.SetAccent(true);
            advancedBtn.SetAccent(false);
        }

        void ShowAdvanced()
        {
            simpleView.Visible = false;
            advancedView.Visible = true;
            simpleBtn.SetAccent(false);
            advancedBtn.SetAccent(true);
        }

        void RefreshSummary()
        {
            if (summaryLabel == null || outputBox == null) return;
            var parts = new List<string>();
            parts.Add(outputBox.Text.ToUpperInvariant());
            parts.Add(displayModeBox.SelectedIndex >= 0 ? (string)displayModeBox.Items[displayModeBox.SelectedIndex] : "");
            parts.Add(upscalerBox.SelectedIndex >= 0 ? (string)upscalerBox.Items[upscalerBox.SelectedIndex] : "");
            parts.Add(Loc.T("vsync") + (vsyncQuick.Checked ? " ✓" : " ✗"));
            if (redzoneQuick != null && redzoneQuick.Checked) parts.Add(Loc.T("redzone_short"));
            summaryLabel.Text = string.Join("   •   ", parts.ToArray()).ToUpperInvariant();
        }

        void OpenFolder()
        {
            var folder = Directory.Exists(GameFolder) ? GameFolder : baseDir;
            try { Process.Start("explorer.exe", "\"" + folder + "\""); } catch (Exception) { }
        }

        void BrowseGameFolder()
        {
            using (var dlg = new FolderBrowserDialog { SelectedPath = Directory.Exists(GameFolder) ? GameFolder : baseDir, ShowNewFolderButton = true })
            {
                if (dlg.ShowDialog(this) == DialogResult.OK) gameFolderBox.Text = dlg.SelectedPath;
            }
        }

        // ------------------------------------------------------------------ PKG installation
        void InstallPkg()
        {
            if (!File.Exists(gameExe))
            {
                MessageBox.Show(this, Loc.F("no_exe", gameExe), "NHL24 PC", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            string pkgFile;
            using (var dlg = new OpenFileDialog { Filter = "PS4 package (*.pkg)|*.pkg|All files (*.*)|*.*", Title = Loc.T("install_pkg") })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                pkgFile = dlg.FileName;
            }
            var folder = GameFolder;
            if (folder.Length == 0) folder = Path.Combine(baseDir, "game");
            gameFolderBox.Text = folder;
            var folderArg = folder.TrimEnd('\\');

            // phase 1: package information only (nothing is written yet)
            string info = RunPkgInfo(pkgFile, folderArg);
            if (info == null) return;
            string infoLine = null, target = folderArg;
            foreach (var line in info.Split('\n'))
            {
                var l = line.Trim();
                if (l.StartsWith("pkg: ")) infoLine = l;
                else if (l.StartsWith("target: ")) target = l.Substring(8).Trim();
                else if (l.StartsWith("error: "))
                {
                    MessageBox.Show(this, Loc.F("pkg_error", l.Substring(7)), Loc.T("pkg_title"), MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }
            }
            if (infoLine == null)
            {
                MessageBox.Show(this, Loc.F("pkg_error", info.Trim()), Loc.T("pkg_title"), MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            var titleId = Field(infoLine, "title_id");
            var kind = Field(infoLine, "kind");
            var ver = Field(infoLine, "app_ver");
            var title = Field(infoLine, "title");
            long size = 0;
            long.TryParse(Field(infoLine, "size"), out size);
            if (titleId != TitleId || (kind != "base" && kind != "patch"))
            {
                MessageBox.Show(this, Loc.F("pkg_wrong", titleId, title.Length > 0 ? title : Loc.T("pkg_kind_other")), Loc.T("pkg_title"), MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            var kindText = kind == "base" ? Loc.T("pkg_kind_base") : Loc.T("pkg_kind_patch");
            if (MessageBox.Show(this, Loc.F("pkg_confirm", kindText, title, ver, size / (1024 * 1024), target), Loc.T("pkg_title"),
                    MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
                return;

            // phase 2: extraction with progress
            pkgTarget = target;
            pkgKind = kind;
            progressTitle.Text = kindText + ": " + title + " v" + ver;
            progressFile.Text = Path.GetFileName(pkgFile);
            progressNumbers.Text = "";
            progressBar.Value = 0;
            progressCancel.Text = Loc.T("cancel");
            progressPanel.Visible = true;
            progressPanel.BringToFront();

            var psi = new ProcessStartInfo(gameExe)
            {
                Arguments = "--extract-pkg \"" + pkgFile + "\" --extract-to \"" + folderArg + "\"",
                WorkingDirectory = baseDir,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
            };
            try
            {
                pkgProcess = new Process { StartInfo = psi, EnableRaisingEvents = true };
                pkgProcess.OutputDataReceived += (s, e) => { if (e.Data != null) BeginInvoke(new Action<string>(OnPkgLine), e.Data); };
                pkgProcess.Exited += (s, e) => { BeginInvoke(new Action(OnPkgExited)); };
                pkgProcess.Start();
                pkgProcess.BeginOutputReadLine();
                pkgProcess.BeginErrorReadLine();
            }
            catch (Exception ex)
            {
                pkgProcess = null;
                progressTitle.Text = Loc.F("pkg_error", ex.Message);
                progressCancel.Text = Loc.T("close");
            }
        }

        // "nhl24.exe --extract-pkg <file> --extract-to <folder> --pkg-info": header + param.sfo only
        string RunPkgInfo(string pkgFile, string folderArg)
        {
            var psi = new ProcessStartInfo(gameExe)
            {
                Arguments = "--extract-pkg \"" + pkgFile + "\" --extract-to \"" + folderArg + "\" --pkg-info",
                WorkingDirectory = baseDir,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
            };
            try
            {
                Cursor = Cursors.WaitCursor;
                using (var proc = Process.Start(psi))
                {
                    var output = proc.StandardOutput.ReadToEnd();
                    if (!proc.WaitForExit(60000)) { try { proc.Kill(); } catch (Exception) { } }
                    return output;
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, Loc.F("pkg_error", ex.Message), Loc.T("pkg_title"), MessageBoxButtons.OK, MessageBoxIcon.Error);
                return null;
            }
            finally { Cursor = Cursors.Default; }
        }

        static string Field(string line, string key)
        {
            // key=value or key="value" inside the "pkg:" line
            var idx = line.IndexOf(" " + key + "=", StringComparison.Ordinal);
            if (idx < 0) return "";
            idx += key.Length + 2;
            if (idx < line.Length && line[idx] == '"')
            {
                var end = line.IndexOf('"', idx + 1);
                return end < 0 ? line.Substring(idx + 1) : line.Substring(idx + 1, end - idx - 1);
            }
            var sp = line.IndexOf(' ', idx);
            return sp < 0 ? line.Substring(idx) : line.Substring(idx, sp - idx);
        }

        void OnPkgLine(string line)
        {
            if (line.StartsWith("pkg: "))
            {
                pkgKind = Field(line, "kind");
            }
            else if (line.StartsWith("target: "))
            {
                pkgTarget = line.Substring(8).Trim();
                progressFile.Text = pkgTarget;
            }
            else if (line.StartsWith("progress "))
            {
                var p = line.Split(new[] { ' ' }, 6);
                if (p.Length < 6) return;
                long filesDone, filesTotal, bytesDone, bytesTotal;
                long.TryParse(p[1], out filesDone); long.TryParse(p[2], out filesTotal);
                long.TryParse(p[3], out bytesDone); long.TryParse(p[4], out bytesTotal);
                progressFile.Text = Loc.F("pkg_extracting", p[5]);
                progressNumbers.Text = Loc.F("pkg_progress", bytesDone / (1024 * 1024), bytesTotal / (1024 * 1024), filesDone, filesTotal);
                progressBar.Value = bytesTotal > 0 ? (int)Math.Min(1000, bytesDone * 1000 / bytesTotal) : 0;
            }
            else if (line.StartsWith("error: "))
            {
                progressTitle.Text = Loc.F("pkg_error", line.Substring(7));
                progressCancel.Text = Loc.T("close");
            }
            else if (line == "done")
            {
                progressBar.Value = 1000;
                progressTitle.Text = Loc.F("pkg_done", pkgKind == "patch" ? Loc.T("pkg_kind_patch") : Loc.T("pkg_kind_base"), pkgTarget);
                progressFile.Text = "";
                progressCancel.Text = Loc.T("close");
                RefreshReadyCheck();
            }
        }

        void OnPkgExited()
        {
            if (pkgProcess == null) return;
            int code = -1;
            try { code = pkgProcess.ExitCode; } catch (Exception) { }
            pkgProcess = null;
            if (code != 0 && progressCancel.Text != Loc.T("close"))
            {
                progressTitle.Text = Loc.T("cancelled");
                progressCancel.Text = Loc.T("close");
            }
            RefreshReadyCheck();
        }

        void CancelOrClosePkg()
        {
            if (pkgProcess != null)
            {
                try { pkgProcess.Kill(); } catch (Exception) { }
                progressTitle.Text = Loc.T("cancelled");
                progressCancel.Text = Loc.T("close");
                return;
            }
            progressPanel.Visible = false;
        }

        // ------------------------------------------------------------------ bindings UI
        void RefreshBindList()
        {
            bindList.Items.Clear();
            foreach (var a in actions)
            {
                var item = new ListViewItem(a.Label) { Tag = a };
                item.SubItems.Add(bindings[a.Id]);
                bindList.Items.Add(item);
            }
        }

        bool ImageToBox(int ix, int iy, out float bx, out float by)
        {
            bx = by = 0;
            if (padImage == null) return false;
            float scale = Math.Min((float)padBox.Width / padImage.Width, (float)padBox.Height / padImage.Height);
            float ox = (padBox.Width - padImage.Width * scale) / 2f;
            float oy = (padBox.Height - padImage.Height * scale) / 2f;
            bx = ox + ix * scale; by = oy + iy * scale;
            return true;
        }

        void PadPaint(object sender, PaintEventArgs e)
        {
            if (padImage == null)
            {
                e.Graphics.DrawString("ps4_pad.png ?", Font, Brushes.Red, 10, 10);
                return;
            }
            using (var font = new Font("Segoe UI", 8, FontStyle.Bold))
            foreach (var a in actions)
            {
                float bx, by;
                if (!ImageToBox(a.X, a.Y, out bx, out by)) continue;
                bool active = capturing == a;
                var brush = active ? Brushes.Orange : Brushes.LightGreen;
                e.Graphics.FillEllipse(brush, bx - 6, by - 6, 12, 12);
                e.Graphics.DrawEllipse(Pens.Black, bx - 6, by - 6, 12, 12);
                var text = bindings[a.Id];
                var size = e.Graphics.MeasureString(text, font);
                e.Graphics.FillRectangle(Brushes.White, bx + 7, by - size.Height / 2, size.Width + 2, size.Height);
                e.Graphics.DrawString(text, font, Brushes.Black, bx + 8, by - size.Height / 2);
            }
        }

        void PadClick(object sender, MouseEventArgs e)
        {
            PadAction best = null; float bestDist = 24f;
            foreach (var a in actions)
            {
                float bx, by;
                if (!ImageToBox(a.X, a.Y, out bx, out by)) continue;
                float d = (float)Math.Sqrt((bx - e.X) * (bx - e.X) + (by - e.Y) * (by - e.Y));
                if (d < bestDist) { bestDist = d; best = a; }
            }
            if (best != null) BeginCapture(best);
        }

        void BeginCapture(PadAction a)
        {
            capturing = a;
            captureLabel.Text = Loc.F("press_key", a.Label);
            padBox.Invalidate();
            Focus();
        }

        void OnKeyDown(object sender, KeyEventArgs e)
        {
            if (capturing == null) return;
            e.Handled = true; e.SuppressKeyPress = true;
            if (e.KeyCode == Keys.Escape) { capturing = null; captureLabel.Text = ""; padBox.Invalidate(); return; }
            var name = KeyName(e.KeyCode);
            if (name == null) { captureLabel.Text = Loc.T("key_unusable"); return; }
            bindings[capturing.Id] = name;
            captureLabel.Text = capturing.Label + " = " + name;
            capturing = null;
            RefreshBindList();
            padBox.Invalidate();
            RefreshReadyCheck();
        }

        // WinForms key -> name understood by the input config
        static string KeyName(Keys k)
        {
            if (k >= Keys.A && k <= Keys.Z) return k.ToString().ToLowerInvariant();
            if (k >= Keys.D0 && k <= Keys.D9) return ((int)(k - Keys.D0)).ToString();
            if (k >= Keys.F1 && k <= Keys.F12) return "f" + ((int)(k - Keys.F1) + 1);
            if (k >= Keys.NumPad0 && k <= Keys.NumPad9) return "kp" + ((int)(k - Keys.NumPad0));
            switch (k)
            {
                case Keys.Up: return "up";
                case Keys.Down: return "down";
                case Keys.Left: return "left";
                case Keys.Right: return "right";
                case Keys.Space: return "space";
                case Keys.Enter: return "enter";
                case Keys.Tab: return "tab";
                case Keys.Back: return "backspace";
                case Keys.Delete: return "delete";
                case Keys.Insert: return "insert";
                case Keys.Home: return "home";
                case Keys.End: return "end";
                case Keys.PageUp: return "pgup";
                case Keys.PageDown: return "pgdown";
                case Keys.CapsLock: return "capslock";
                case Keys.ShiftKey: case Keys.LShiftKey: return "lshift";
                case Keys.RShiftKey: return "rshift";
                case Keys.ControlKey: case Keys.LControlKey: return "lctrl";
                case Keys.RControlKey: return "rctrl";
                case Keys.Menu: case Keys.LMenu: return "lalt";
                case Keys.RMenu: return "ralt";
                case Keys.Decimal: return "kpperiod";
                case Keys.Divide: return "kpslash";
                case Keys.Multiply: return "kpasterisk";
                case Keys.Subtract: return "kpminus";
                case Keys.Add: return "kpplus";
                case Keys.Oemcomma: return "comma";
                case Keys.OemPeriod: return "period";
                case Keys.OemQuestion: return "slash";
                case Keys.OemSemicolon: return "semicolon";
                case Keys.OemQuotes: return "apostrophe";
                case Keys.OemOpenBrackets: return "lbracket";
                case Keys.OemCloseBrackets: return "rbracket";
                case Keys.OemMinus: return "minus";
                case Keys.Oemplus: return "equals";
                case Keys.Oemtilde: return "grave";
                case Keys.OemPipe: return "backslash";
            }
            return null;
        }

        // ------------------------------------------------------------------ save / run
        bool SaveAll()
        {
            try
            {
                SaveBindings();
                SaveConfig();
                return true;
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, Loc.F("save_fail", ex.Message), "NHL24 PC", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return false;
            }
        }

        void SaveAllSilently()
        {
            try { SaveBindings(); SaveConfig(); } catch (Exception) { }
        }

        void RunGame()
        {
            if (!File.Exists(gameExe))
            {
                MessageBox.Show(this, Loc.F("no_exe", gameExe), "NHL24 PC", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            var psi = new ProcessStartInfo(gameExe) { WorkingDirectory = baseDir, UseShellExecute = false };
            Process.Start(psi);
            Close();
        }

        [STAThread]
        public static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new LauncherForm(Array.IndexOf(args, "--advanced") >= 0));
        }
    }
}
