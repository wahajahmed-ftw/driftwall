using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Win32;

namespace Driftwall
{
    class WallpaperItem
    {
        public string Id, Dir, Title, Type, File, Source;
        public Bitmap Thumb;  // already scaled for the gallery tiles

        public string TypeLabel
        {
            get { return Type == "video" ? "Video" : Type == "scene" ? "Live scene" : Type == "image" ? "Image" : Type; }
        }
    }

    class MonitorInfo
    {
        public string Id, DeviceName;
        public int Number;
        public Rectangle Bounds;
        public bool Primary;

        public static List<MonitorInfo> All()
        {
            var list = new List<MonitorInfo>();
            foreach (var s in Screen.AllScreens)
            {
                int n;
                string digits = new string(s.DeviceName.Where(char.IsDigit).ToArray());
                list.Add(new MonitorInfo
                {
                    Id = Native.MonitorId(s.DeviceName),
                    DeviceName = s.DeviceName,
                    Number = int.TryParse(digits, out n) ? n : list.Count + 1,
                    Bounds = s.Bounds,
                    Primary = s.Primary,
                });
            }
            return list.OrderBy(m => m.Number).ToList();
        }
    }

    class Library
    {
        public readonly string DataDir, LibDir, ConfigPath, EnginePath;
        public List<WallpaperItem> Items = new List<WallpaperItem>();
        Dictionary<string, object> config = new Dictionary<string, object>();
        static readonly JavaScriptSerializer Json = new JavaScriptSerializer();

        public Library()
        {
            DataDir = Path.GetDirectoryName(Application.ExecutablePath);
            LibDir = Path.Combine(DataDir, "Library");
            ConfigPath = Path.Combine(DataDir, "config.json");
            EnginePath = Path.Combine(DataDir, "DriftwallEngine.exe");
            Directory.CreateDirectory(LibDir);
        }

        // ---------- loading ----------

        public void Load(int thumbWidth, int thumbHeight)
        {
            foreach (var old in Items) if (old.Thumb != null) old.Thumb.Dispose();
            Items.Clear();
            foreach (var dir in Directory.GetDirectories(LibDir))
            {
                var item = ReadItem(dir);
                if (item == null) continue;
                item.Thumb = LoadThumb(item, thumbWidth, thumbHeight);
                Items.Add(item);
            }
            Items = Items.OrderBy(i => i.Title, StringComparer.CurrentCultureIgnoreCase).ToList();
            LoadConfig();
        }

        static WallpaperItem ReadItem(string dir)
        {
            try
            {
                var j = Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(Path.Combine(dir, "wallpaper.json")));
                return new WallpaperItem
                {
                    Id = Path.GetFileName(dir),
                    Dir = dir,
                    Title = Str(j, "title") ?? Path.GetFileName(dir),
                    Type = Str(j, "type"),
                    File = Str(j, "file"),
                    Source = Str(j, "source"),
                };
            }
            catch { return null; }
        }

        static string Str(Dictionary<string, object> j, string key)
        {
            object v;
            return j.TryGetValue(key, out v) && v != null ? v.ToString() : null;
        }

        static Bitmap LoadThumb(WallpaperItem item, int w, int h)
        {
            string path = Path.Combine(item.Dir, "thumb.jpg");
            if (!File.Exists(path)) return null;
            try
            {
                using (var ms = new MemoryStream(File.ReadAllBytes(path)))
                using (var img = Image.FromStream(ms))
                    return Cover(img, w, h);
            }
            catch { return null; }
        }

        // Scale and crop to fill w x h (like CSS object-fit: cover).
        public static Bitmap Cover(Image img, int w, int h)
        {
            var bmp = new Bitmap(w, h, PixelFormat.Format32bppPArgb);
            using (var g = Graphics.FromImage(bmp))
            {
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                float k = Math.Max((float)w / img.Width, (float)h / img.Height);
                float sw = w / k, sh = h / k;
                g.DrawImage(img, new RectangleF(0, 0, w, h),
                    new RectangleF((img.Width - sw) / 2, (img.Height - sh) / 2, sw, sh), GraphicsUnit.Pixel);
            }
            return bmp;
        }

        // ---------- config shared with the engine ----------

        void LoadConfig()
        {
            try { config = Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(ConfigPath)) ?? new Dictionary<string, object>(); }
            catch { config = new Dictionary<string, object>(); }
            if (!(config.ContainsKey("monitors") && config["monitors"] is Dictionary<string, object>))
                config["monitors"] = new Dictionary<string, object>();
        }

        Dictionary<string, object> Monitors { get { return (Dictionary<string, object>)config["monitors"]; } }

        public string AssignedTo(string monitorId)
        {
            object v;
            return Monitors.TryGetValue(monitorId, out v) && v != null ? v.ToString() : null;
        }

        public WallpaperItem Find(string id)
        {
            return id == null ? null : Items.FirstOrDefault(i => i.Id == id);
        }

        public void Assign(IEnumerable<string> monitorIds, string wallpaperId)
        {
            foreach (var m in monitorIds)
            {
                if (wallpaperId == null) Monitors.Remove(m);
                else Monitors[m] = wallpaperId;
            }
            SaveConfig();
        }

        public bool PauseWhenCovered
        {
            get { object v; return !config.TryGetValue("pauseWhenCovered", out v) || !(v is bool) || (bool)v; }
            set { config["pauseWhenCovered"] = value; SaveConfig(); }
        }

        public int SceneFps
        {
            get { object v; return config.TryGetValue("sceneFps", out v) && v is int ? (int)v : 30; }
            set { config["sceneFps"] = value; SaveConfig(); }
        }

        void SaveConfig()
        {
            string tmp = ConfigPath + ".tmp";
            File.WriteAllText(tmp, Json.Serialize(config), new UTF8Encoding(false));
            if (File.Exists(ConfigPath)) File.Replace(tmp, ConfigPath, null);
            else File.Move(tmp, ConfigPath);
            NotifyEngine();
        }

        // ---------- engine ----------

        public static IntPtr EngineWindow() { return Native.FindWindow("DriftwallEngine", null); }

        public void NotifyEngine()
        {
            IntPtr h = EngineWindow();
            if (h != IntPtr.Zero) Native.PostMessage(h, Native.WM_APP_RELOAD, IntPtr.Zero, IntPtr.Zero);
            else StartEngine();
        }

        public void StartEngine()
        {
            if (EngineWindow() == IntPtr.Zero && File.Exists(EnginePath))
                Process.Start(new ProcessStartInfo(EnginePath) { WorkingDirectory = DataDir, UseShellExecute = false });
        }

        public void StopEngine()
        {
            IntPtr h = EngineWindow();
            if (h != IntPtr.Zero) Native.PostMessage(h, Native.WM_APP_EXIT, IntPtr.Zero, IntPtr.Zero);
        }

        const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";

        public bool StartWithWindows
        {
            get
            {
                using (var k = Registry.CurrentUser.OpenSubKey(RunKey))
                    return k != null && k.GetValue("Driftwall") != null;
            }
            set
            {
                using (var k = Registry.CurrentUser.CreateSubKey(RunKey))
                {
                    if (value) k.SetValue("Driftwall", "\"" + EnginePath + "\"");
                    else k.DeleteValue("Driftwall", false);
                }
            }
        }

        // ---------- editing ----------

        public void Rename(WallpaperItem item, string title)
        {
            var path = Path.Combine(item.Dir, "wallpaper.json");
            var j = Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(path));
            j["title"] = title;
            File.WriteAllText(path, Json.Serialize(j), new UTF8Encoding(false));
            item.Title = title;
        }

        // Unassign first so the engine closes the file, then remove the folder.
        public void Delete(WallpaperItem item)
        {
            var used = Monitors.Where(kv => kv.Value != null && kv.Value.ToString() == item.Id).Select(kv => kv.Key).ToList();
            if (used.Count > 0) Assign(used, null);
            for (int attempt = 0; attempt < 20; attempt++)
            {
                try { Directory.Delete(item.Dir, true); break; }
                catch (IOException) { System.Threading.Thread.Sleep(250); }
                catch (UnauthorizedAccessException) { System.Threading.Thread.Sleep(250); }
            }
            Items.Remove(item);
            if (item.Thumb != null) item.Thumb.Dispose();
        }
    }
}
