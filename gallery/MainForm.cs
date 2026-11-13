using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Forms;

namespace Driftwall
{
    class MainForm : Form
    {
        readonly Library lib = new Library();
        readonly DisplayBar displays = new DisplayBar();
        readonly TileFlow flow = new TileFlow();
        readonly Label status = new Label(), engineStatus = new Label(), hint = new Label(), count = new Label();
        readonly DarkButton addButton, settingsButton;
        readonly List<DarkButton> filters = new List<DarkButton>();
        readonly System.Windows.Forms.Timer engineTimer = new System.Windows.Forms.Timer { Interval = 3000 };
        readonly Image logo;
        string filter = "all";
        bool busy;

        public MainForm()
        {
            using (var g = CreateGraphics()) Theme.Scale = g.DpiX / 96f;
            Text = "Driftwall";
            Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            BackColor = Theme.Background;
            ForeColor = Theme.Text;
            Font = Theme.Font(13);
            AutoScaleMode = AutoScaleMode.None;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(Theme.P(1120), Theme.P(780));
            MinimumSize = new Size(Theme.P(760), Theme.P(560));
            AllowDrop = true;
            var stream = GetType().Assembly.GetManifestResourceStream("Driftwall.logo.png");
            if (stream != null) logo = Image.FromStream(stream);

            // ---- header ----
            var header = new Panel { Dock = DockStyle.Top, Height = Theme.P(84), BackColor = Theme.Background };
            header.Paint += (s, e) =>
            {
                Theme.Smooth(e.Graphics);
                if (logo != null) e.Graphics.DrawImage(logo, Theme.P(28), Theme.P(22), Theme.P(42), Theme.P(42));
                using (var f = Theme.Font(24, true))
                    Theme.DrawText(e.Graphics, "Driftwall", f, Theme.Text, new RectangleF(Theme.P(82), Theme.P(16), Theme.P(300), Theme.P(32)));
            };
            engineStatus.SetBounds(Theme.P(84), Theme.P(48), Theme.P(520), Theme.P(22));
            engineStatus.ForeColor = Theme.Muted;
            engineStatus.Font = Theme.Font(13);
            engineStatus.BackColor = Theme.Background;
            header.Controls.Add(engineStatus);

            addButton = new DarkButton("Add wallpapers", Native.GlyphAdd, true);
            settingsButton = new DarkButton("Settings", Native.GlyphSettings);
            header.Controls.Add(addButton);
            header.Controls.Add(settingsButton);
            header.Resize += (s, e) =>
            {
                settingsButton.Location = new Point(header.Width - settingsButton.Width - Theme.P(28), Theme.P(24));
                addButton.Location = new Point(settingsButton.Left - addButton.Width - Theme.P(10), Theme.P(24));
            };
            addButton.Click += (s, e) => ShowAddMenu();
            settingsButton.Click += (s, e) => ShowSettings();

            // ---- displays ----
            var displaysPanel = new Panel { Dock = DockStyle.Top, Height = Theme.P(250), BackColor = Theme.Panel, Padding = new Padding(Theme.P(20), Theme.P(36), Theme.P(20), Theme.P(8)) };
            displaysPanel.Paint += (s, e) =>
            {
                using (var f = Theme.Font(12, true))
                    Theme.DrawText(e.Graphics, "DISPLAYS", f, Theme.Muted, new RectangleF(Theme.P(28), Theme.P(10), Theme.P(200), Theme.P(22)));
            };
            displays.Dock = DockStyle.Fill;
            displays.BackColor = Theme.Panel;
            displays.CurrentWallpaper = id => lib.Find(lib.AssignedTo(id));
            displays.SelectionChanged += (s, e) => UpdateHint();
            displaysPanel.Controls.Add(displays);

            // ---- library header ----
            var libHeader = new Panel { Dock = DockStyle.Top, Height = Theme.P(64), BackColor = Theme.Background };
            libHeader.Paint += (s, e) =>
            {
                using (var f = Theme.Font(12, true))
                    Theme.DrawText(e.Graphics, "LIBRARY", f, Theme.Muted, new RectangleF(Theme.P(28), Theme.P(14), Theme.P(80), Theme.P(36)));
            };
            count.SetBounds(Theme.P(96), Theme.P(14), Theme.P(44), Theme.P(36));
            count.TextAlign = ContentAlignment.MiddleLeft;
            count.ForeColor = Theme.Muted;
            libHeader.Controls.Add(count);
            int fx = Theme.P(150);
            foreach (var f in new[] { new[] { "all", "All" }, new[] { "video", "Videos" }, new[] { "scene", "Live scenes" }, new[] { "image", "Images" } })
            {
                var b = new DarkButton(f[1]) { Tag = f[0], Height = Theme.P(32), Selected = f[0] == "all" };
                b.Location = new Point(fx, Theme.P(16));
                fx += b.Width + Theme.P(8);
                b.Click += (s, e) => { filter = (string)((Control)s).Tag; foreach (var x in filters) { x.Selected = x == s; x.Invalidate(); } RebuildTiles(); };
                filters.Add(b);
                libHeader.Controls.Add(b);
            }
            hint.ForeColor = Theme.Muted;
            hint.TextAlign = ContentAlignment.MiddleRight;
            hint.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            hint.SetBounds(libHeader.Width - Theme.P(420), Theme.P(14), Theme.P(392), Theme.P(36));
            libHeader.Controls.Add(hint);
            libHeader.Resize += (s, e) => hint.Left = libHeader.Width - hint.Width - Theme.P(28);

            // ---- grid + footer ----
            flow.Dock = DockStyle.Fill;
            flow.Padding = new Padding(Theme.P(20), 0, Theme.P(20), Theme.P(20));
            var footer = new Panel { Dock = DockStyle.Bottom, Height = Theme.P(34), BackColor = Theme.Panel };
            status.Dock = DockStyle.Fill;
            status.ForeColor = Theme.Muted;
            status.TextAlign = ContentAlignment.MiddleLeft;
            status.Padding = new Padding(Theme.P(24), 0, 0, 0);
            status.Text = "Tip: drag videos or images onto this window to add them.";
            footer.Controls.Add(status);

            Controls.Add(libHeader);
            Controls.Add(displaysPanel);
            Controls.Add(header);
            Controls.Add(footer);
            Controls.Add(flow);
            flow.BringToFront();

            foreach (Control c in new Control[] { this, flow, displays, header, libHeader, displaysPanel }) HookDrop(c);
            engineTimer.Tick += (s, e) => UpdateEngineStatus();
            engineTimer.Start();
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            int on = 1;
            Native.DwmSetWindowAttribute(Handle, 20, ref on, 4);  // dark title bar
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            lib.StartEngine();
            Reload();
            UpdateEngineStatus();
        }

        // ---------- data ----------

        void Reload()
        {
            lib.Load(Theme.P(240), Theme.P(135));
            displays.Monitors = MonitorInfo.All();
            if (displays.SelectedId == null || !displays.Monitors.Any(m => m.Id == displays.SelectedId))
                displays.SelectedId = displays.Monitors.Select(m => m.Id).FirstOrDefault();
            displays.Invalidate();
            RebuildTiles();
            UpdateHint();
        }

        void RebuildTiles()
        {
            flow.SuspendLayout();
            foreach (Control c in flow.Controls) c.Dispose();
            flow.Controls.Clear();
            var items = lib.Items.Where(i => filter == "all" || i.Type == filter).ToList();
            foreach (var item in items)
            {
                var tile = new WallpaperTile(item);
                var nums = displays.Monitors.Where(m => lib.AssignedTo(m.Id) == item.Id).Select(m => m.Number.ToString()).ToList();
                if (nums.Count > 0) tile.InUse = (nums.Count == 1 ? "Display " : "Displays ") + string.Join(" + ", nums);
                tile.MouseClick += (s, e) => { if (e.Button == MouseButtons.Left) SetOnSelected(item); };
                tile.MouseUp += (s, e) => { if (e.Button == MouseButtons.Right) ShowTileMenu(tile, e.Location); };
                HookDrop(tile);
                flow.Controls.Add(tile);
            }
            if (items.Count == 0)
            {
                var empty = new Label
                {
                    Text = lib.Items.Count == 0 ? "Your library is empty. Click \"Add wallpapers\" or drop video files here." : "Nothing here yet.",
                    ForeColor = Theme.Muted, AutoSize = true, Margin = new Padding(Theme.P(10), Theme.P(20), 0, 0),
                };
                flow.Controls.Add(empty);
            }
            flow.ResumeLayout();
            count.Text = lib.Items.Count.ToString();
        }

        void UpdateHint()
        {
            var m = displays.Monitors.FirstOrDefault(x => x.Id == displays.SelectedId);
            hint.Text = m == null ? "" : "Click a wallpaper to set it on Display " + m.Number;
        }

        void SetOnSelected(WallpaperItem item)
        {
            if (displays.SelectedId == null) return;
            Assign(new[] { displays.SelectedId }, item);
        }

        void Assign(IEnumerable<string> monitorIds, WallpaperItem item)
        {
            lib.Assign(monitorIds, item == null ? null : item.Id);
            displays.Invalidate();
            RebuildTiles();
            status.Text = item == null ? "Live wallpaper removed." : "\"" + item.Title + "\" is now on your desktop.";
        }

        // ---------- menus ----------

        void ShowAddMenu()
        {
            var menu = DarkMenuRenderer.Menu();
            menu.Items.Add("Video or image files…", null, (s, e) => PickFiles());
            menu.Items.Add("Import my Lively wallpapers", null, (s, e) => RunImport(imp => imp.ImportLivelyLibrary()));
            menu.Show(addButton, new Point(0, addButton.Height + Theme.P(4)));
        }

        void PickFiles()
        {
            using (var dlg = new OpenFileDialog
            {
                Title = "Add wallpapers",
                Multiselect = true,
                Filter = "Videos and images|*.mp4;*.m4v;*.mov;*.mkv;*.webm;*.avi;*.wmv;*.jpg;*.jpeg;*.png;*.webp;*.bmp|All files|*.*",
            })
            {
                if (dlg.ShowDialog(this) == DialogResult.OK)
                {
                    var files = dlg.FileNames;
                    RunImport(imp => imp.ImportPaths(files));
                }
            }
        }

        void ShowTileMenu(WallpaperTile tile, Point at)
        {
            var item = tile.Item;
            var menu = DarkMenuRenderer.Menu();
            foreach (var m in displays.Monitors)
            {
                var mon = m;
                menu.Items.Add("Set on Display " + m.Number + (m.Primary ? " (main)" : ""), null, (s, e) => Assign(new[] { mon.Id }, item));
            }
            if (displays.Monitors.Count > 1)
                menu.Items.Add("Set on all displays", null, (s, e) => Assign(displays.Monitors.Select(x => x.Id), item));
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add("Rename…", null, (s, e) => RenameItem(item));
            menu.Items.Add("Show in folder", null, (s, e) => Process.Start("explorer.exe", "\"" + item.Dir + "\""));
            menu.Items.Add(new ToolStripSeparator());
            var del = menu.Items.Add("Delete from library", null, (s, e) => DeleteItem(item));
            del.Tag = "danger";
            menu.Show(tile, at);
        }

        void RenameItem(WallpaperItem item)
        {
            string name = Prompt("Rename wallpaper", item.Title);
            if (string.IsNullOrWhiteSpace(name)) return;
            lib.Rename(item, name.Trim());
            displays.Invalidate();
            RebuildTiles();
        }

        void DeleteItem(WallpaperItem item)
        {
            if (MessageBox.Show(this, "Delete \"" + item.Title + "\" from your library?\n\nThis removes its files from " + lib.LibDir + ".",
                    "Driftwall", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
            Cursor = Cursors.WaitCursor;
            lib.Delete(item);
            Cursor = Cursors.Default;
            displays.Invalidate();
            RebuildTiles();
            status.Text = "Deleted \"" + item.Title + "\".";
        }

        // ---------- import ----------

        void HookDrop(Control c)
        {
            c.AllowDrop = true;
            c.DragEnter += (s, e) =>
            {
                e.Effect = !busy && e.Data.GetDataPresent(DataFormats.FileDrop) ? DragDropEffects.Copy : DragDropEffects.None;
                if (e.Effect == DragDropEffects.Copy) status.Text = "Drop to add to your library";
            };
            c.DragDrop += (s, e) =>
            {
                var paths = e.Data.GetData(DataFormats.FileDrop) as string[];
                if (paths != null && paths.Length > 0) RunImport(imp => imp.ImportPaths(paths));
            };
        }

        void RunImport(Action<Importer> work)
        {
            if (busy) return;
            busy = true;
            addButton.Enabled = false;
            addButton.Invalidate();
            var imp = new Importer(lib)
            {
                Progress = msg => BeginInvoke((Action)(() => status.Text = msg)),
                Confirm = q => (bool)Invoke((Func<bool>)(() =>
                    MessageBox.Show(this, q, "Driftwall", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)),
            };
            var t = new Thread(() =>
            {
                try { work(imp); }
                catch (Exception ex) { imp.Problems.Add(ex.Message); }
                BeginInvoke((Action)(() => ImportDone(imp)));
            });
            t.SetApartmentState(ApartmentState.STA);
            t.IsBackground = true;
            t.Start();
        }

        void ImportDone(Importer imp)
        {
            busy = false;
            addButton.Enabled = true;
            addButton.Invalidate();
            Reload();
            status.Text = imp.Added.Count == 0 ? "Nothing was added." :
                "Added " + imp.Added.Count + " wallpaper" + (imp.Added.Count == 1 ? "" : "s") + ". Every file was scanned by Microsoft Defender.";
            if (imp.Problems.Count > 0)
                MessageBox.Show(this, string.Join("\n", imp.Problems.Take(15)) + (imp.Problems.Count > 15 ? "\n…" : ""),
                    "Driftwall", MessageBoxButtons.OK, imp.Problems.Any(p => p.Contains("threat")) ? MessageBoxIcon.Warning : MessageBoxIcon.Information);
        }

        // ---------- engine status ----------

        void UpdateEngineStatus()
        {
            var procs = Process.GetProcessesByName("DriftwallEngine");
            if (procs.Length == 0) { engineStatus.Text = "Engine stopped  ·  open Settings to start it"; return; }
            if (!engineStatus.Text.StartsWith("Engine running")) engineStatus.Text = "Engine running";
            ThreadPool.QueueUserWorkItem(_ =>
            {
                string mem = "";
                try
                {
                    using (var pc = new PerformanceCounter("Process", "Working Set - Private", "DriftwallEngine", true))
                        mem = "  ·  using " + (pc.NextValue() / (1024 * 1024)).ToString("0") + " MB";
                }
                catch { }
                try { BeginInvoke((Action)(() => engineStatus.Text = "Engine running" + mem)); } catch { }
            });
            foreach (var p in procs) p.Dispose();
        }

        // ---------- dialogs ----------

        Form DarkDialog(string title, int w, int h)
        {
            var f = new Form
            {
                Text = title, FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false, MinimizeBox = false,
                StartPosition = FormStartPosition.CenterParent, ClientSize = new Size(Theme.P(w), Theme.P(h)),
                BackColor = Theme.Background, ForeColor = Theme.Text, Font = Theme.Font(13), ShowInTaskbar = false,
                AutoScaleMode = AutoScaleMode.None, Icon = Icon,
            };
            f.HandleCreated += (s, e) => { int on = 1; Native.DwmSetWindowAttribute(f.Handle, 20, ref on, 4); };
            return f;
        }

        string Prompt(string title, string value)
        {
            using (var f = DarkDialog(title, 420, 140))
            {
                var box = new TextBox { Text = value, BackColor = Theme.Card, ForeColor = Theme.Text, BorderStyle = BorderStyle.FixedSingle };
                box.SetBounds(Theme.P(20), Theme.P(24), Theme.P(380), Theme.P(30));
                var ok = new DarkButton("Save", null, true);
                ok.Location = new Point(Theme.P(400) - ok.Width, Theme.P(80));
                ok.Click += (s, e) => { f.DialogResult = DialogResult.OK; f.Close(); };
                f.Controls.Add(box);
                f.Controls.Add(ok);
                f.AcceptButton = null;
                box.KeyDown += (s, e) => { if (e.KeyCode == Keys.Enter) { f.DialogResult = DialogResult.OK; f.Close(); } };
                return f.ShowDialog(this) == DialogResult.OK ? box.Text : null;
            }
        }

        void ShowSettings()
        {
            using (var f = DarkDialog("Settings  ·  Driftwall " + Application.ProductVersion, 520, 400))
            {
                int y = Theme.P(20);
                Func<string, string, bool, CheckBox> check = (text, detail, value) =>
                {
                    var c = new CheckBox { Text = text, Checked = value, ForeColor = Theme.Text, AutoSize = true, Font = Theme.Font(14, true) };
                    c.Location = new Point(Theme.P(20), y);
                    var d = new Label { Text = detail, ForeColor = Theme.Muted, AutoSize = false };
                    d.SetBounds(Theme.P(38), y + Theme.P(26), Theme.P(460), Theme.P(36));
                    f.Controls.Add(c);
                    f.Controls.Add(d);
                    y += Theme.P(72);
                    return c;
                };
                var startup = check("Start with Windows", "Your wallpapers come back automatically when you sign in.", lib.StartWithWindows);
                var pause = check("Pause when a window covers the display",
                    "Saves CPU and GPU while you work, watch videos or play games full screen.", lib.PauseWhenCovered);
                var smooth = check("Smooth live scenes", "Live scenes run at each display's refresh rate (for example 144 Hz) in sync with the screen. Off: 30 fps, which uses less GPU.",
                    lib.SceneFps <= 0 || lib.SceneFps >= 60);

                var lbl = new Label { Text = "Library folder: " + lib.LibDir, ForeColor = Theme.Muted, AutoSize = false };
                lbl.SetBounds(Theme.P(20), y + Theme.P(4), Theme.P(480), Theme.P(22));
                f.Controls.Add(lbl);
                y += Theme.P(40);

                bool running = Library.EngineWindow() != IntPtr.Zero;
                var engineBtn = new DarkButton(running ? "Stop engine" : "Start engine", running ? Native.GlyphStop : Native.GlyphPlay);
                engineBtn.Location = new Point(Theme.P(20), y);
                engineBtn.Click += (s, e) =>
                {
                    if (Library.EngineWindow() != IntPtr.Zero) lib.StopEngine(); else lib.StartEngine();
                    f.Close();
                };
                var folderBtn = new DarkButton("Open library folder", Native.GlyphFolder);
                folderBtn.Location = new Point(engineBtn.Right + Theme.P(10), y);
                folderBtn.Click += (s, e) => Process.Start("explorer.exe", "\"" + lib.LibDir + "\"");
                f.Controls.Add(engineBtn);
                f.Controls.Add(folderBtn);

                var done = new DarkButton("Done", null, true);
                done.Location = new Point(Theme.P(500) - done.Width, Theme.P(400) - done.Height - Theme.P(16));
                done.Click += (s, e) => f.Close();
                f.Controls.Add(done);

                f.ShowDialog(this);
                if (startup.Checked != lib.StartWithWindows) lib.StartWithWindows = startup.Checked;
                if (pause.Checked != lib.PauseWhenCovered) lib.PauseWhenCovered = pause.Checked;
                int fps = smooth.Checked ? 0 : 30;  // 0: match each display's refresh rate
                if (fps != lib.SceneFps) lib.SceneFps = fps;
                UpdateEngineStatus();
            }
        }
    }
}
