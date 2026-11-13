using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace Driftwall
{
    // Adds files to the library: checks the real file type, scans with Microsoft Defender, copies, makes a thumbnail.
    // Runs on a background STA thread (the shell thumbnail API needs STA).
    class Importer
    {
        public Action<string> Progress = delegate { };
        public Func<string, bool> Confirm = delegate { return false; };  // asked on the UI thread by the caller
        public readonly List<string> Added = new List<string>(), Problems = new List<string>();

        readonly Library lib;
        static readonly JavaScriptSerializer Json = new JavaScriptSerializer();
        static readonly string[] VideoExt = { ".mp4", ".m4v", ".mov", ".mkv", ".webm", ".avi", ".wmv" };
        static readonly string[] ImageExt = { ".jpg", ".jpeg", ".png", ".webp", ".bmp" };

        public Importer(Library library) { lib = library; }

        // ---------- entry points ----------

        public void ImportPaths(IEnumerable<string> paths)
        {
            foreach (var p in paths)
            {
                if (Directory.Exists(p) && File.Exists(Path.Combine(p, "LivelyInfo.json"))) ImportLivelyFolder(p);
                else if (Directory.Exists(p)) foreach (var f in Directory.GetFiles(p)) ImportFile(f, null, null, null);
                else if (File.Exists(p)) ImportFile(p, null, null, null);
            }
        }

        public static IEnumerable<string> LivelyLibraryRoots()
        {
            string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            var roots = new List<string> { Path.Combine(local, "Lively Wallpaper", "Library") };
            string packages = Path.Combine(local, "Packages");
            if (Directory.Exists(packages))
                foreach (var pkg in Directory.GetDirectories(packages, "*LivelyWallpaper*"))
                    roots.Add(Path.Combine(pkg, "LocalCache", "Local", "Lively Wallpaper", "Library"));
            return roots.Where(Directory.Exists);
        }

        public void ImportLivelyLibrary()
        {
            int found = 0;
            foreach (var root in LivelyLibraryRoots())
                foreach (var sub in new[] { "wallpapers", Path.Combine("SaveData", "wptmp") })
                {
                    string dir = Path.Combine(root, sub);
                    if (!Directory.Exists(dir)) continue;
                    foreach (var w in Directory.GetDirectories(dir))
                        if (File.Exists(Path.Combine(w, "LivelyInfo.json"))) { found++; ImportLivelyFolder(w); }
                }
            if (found == 0) Problems.Add("No Lively wallpapers were found on this PC.");
        }

        // ---------- Lively ----------

        void ImportLivelyFolder(string dir)
        {
            Dictionary<string, object> info;
            try { info = Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(Path.Combine(dir, "LivelyInfo.json"))); }
            catch { Problems.Add(Path.GetFileName(dir) + ": unreadable LivelyInfo.json"); return; }
            string title = Get(info, "Title") ?? Path.GetFileName(dir);
            if (lib.Items.Any(i => string.Equals(i.Title, title, StringComparison.CurrentCultureIgnoreCase))) return;  // already in the library

            string file = Get(info, "FileName");
            bool absolute = Get(info, "IsAbsolutePath") == "True";
            string media = string.IsNullOrEmpty(file) ? null : (absolute ? file : Path.Combine(dir, file));
            string thumb = Get(info, "Thumbnail");
            string thumbPath = string.IsNullOrEmpty(thumb) ? null : Path.Combine(dir, thumb);

            if (media != null && File.Exists(media) && (Detect(media) == "video" || Detect(media) == "image"))
                ImportFile(media, title, thumbPath, "Imported from Lively");
            else
                Problems.Add(title + ": web/app wallpapers from Lively aren't supported yet");
        }

        static string Get(Dictionary<string, object> j, string key)
        {
            object v;
            return j.TryGetValue(key, out v) && v != null ? v.ToString() : null;
        }

        // ---------- files ----------

        // Identify by content, not by extension.
        public static string Detect(string path)
        {
            var h = new byte[16];
            try
            {
                using (var f = File.OpenRead(path))
                    if (f.Read(h, 0, 16) < 12) return null;
            }
            catch { return null; }
            Func<int, string, bool> at = (o, s) => Encoding.ASCII.GetString(h, o, s.Length) == s;
            if (at(4, "ftyp")) return "video";                                            // MP4 / MOV / M4V
            if (h[0] == 0x1A && h[1] == 0x45 && h[2] == 0xDF && h[3] == 0xA3) return "video";  // MKV / WebM
            if (at(0, "RIFF") && at(8, "AVI ")) return "video";
            if (h[0] == 0x30 && h[1] == 0x26 && h[2] == 0xB2 && h[3] == 0x75) return "video";  // WMV / ASF
            if (h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) return "image";              // JPEG
            if (h[0] == 0x89 && at(1, "PNG")) return "image";
            if (at(0, "RIFF") && at(8, "WEBP")) return "image";
            if (at(0, "BM")) return "image";
            if (at(0, "GIF8")) return "gif";
            return null;
        }

        void ImportFile(string path, string title, string thumbSource, string source)
        {
            string name = Path.GetFileName(path);
            string type = Detect(path);
            if (type == "gif") { Problems.Add(name + ": GIFs aren't supported yet"); return; }
            if (type == null) { Problems.Add(name + ": not a video or image file"); return; }
            string ext = Path.GetExtension(path).ToLowerInvariant();
            if (!(type == "video" ? VideoExt : ImageExt).Contains(ext)) ext = type == "video" ? ".mp4" : ".jpg";

            Progress("Scanning " + name + " with Microsoft Defender…");
            string verdict = Scan(path);
            if (verdict == "threat")
            {
                Problems.Add(name + ": Microsoft Defender detected a threat. The file was NOT added.");
                return;
            }
            if (verdict != "clean" && !Confirm("Microsoft Defender couldn't scan \"" + name + "\" (" + verdict + ").\n\nAdd it anyway?"))
            {
                Problems.Add(name + ": skipped (not scanned)");
                return;
            }

            if (title == null) title = CleanTitle(Path.GetFileNameWithoutExtension(path));
            string id = NewId(title);
            string dir = Path.Combine(lib.LibDir, id);
            Directory.CreateDirectory(dir);
            try
            {
                Progress("Copying " + name + "…");
                string file = (type == "video" ? "video" : "image") + ext;
                File.Copy(path, Path.Combine(dir, file));

                Progress("Making a thumbnail for " + name + "…");
                MakeThumb(Path.Combine(dir, file), thumbSource, Path.Combine(dir, "thumb.jpg"));

                var j = new Dictionary<string, object>
                {
                    { "title", title }, { "type", type }, { "file", file }, { "thumbnail", "thumb.jpg" },
                    { "source", source ?? "Added from " + name }, { "added", DateTime.Now.ToString("s") },
                };
                File.WriteAllText(Path.Combine(dir, "wallpaper.json"), Json.Serialize(j), new UTF8Encoding(false));
                Added.Add(id);
            }
            catch (Exception e)
            {
                Problems.Add(name + ": " + e.Message);
                try { Directory.Delete(dir, true); } catch { }
            }
        }

        static void MakeThumb(string media, string thumbSource, string outPath)
        {
            Bitmap src = null;
            try
            {
                if (thumbSource != null && File.Exists(thumbSource))
                    using (var ms = new MemoryStream(File.ReadAllBytes(thumbSource))) src = new Bitmap(Image.FromStream(ms));
                if (src == null) src = Native.ShellThumbnail(media, 480);
                if (src == null) return;
                using (var t = Library.Cover(src, 480, 270)) t.Save(outPath, ImageFormat.Jpeg);
            }
            catch { }
            finally { if (src != null) src.Dispose(); }
        }

        static string CleanTitle(string s)
        {
            s = Regex.Replace(s, @"^\d{6,}[-_ ]*", "");                       // leading upload ids
            s = Regex.Replace(s, @"\d{3,4}x\d{3,4}", "");                      // resolutions
            s = Regex.Replace(s, @"(?i)\b(live|wallpaper|4k|hd|uhd)\b", "");
            s = Regex.Replace(s, @"[-_.]+", " ");
            s = Regex.Replace(s, @"\s+", " ").Trim();
            if (s.Length == 0) return "Wallpaper";
            return char.ToUpper(s[0]) + s.Substring(1);
        }

        string NewId(string title)
        {
            string slug = Regex.Replace(title.ToLowerInvariant(), @"[^a-z0-9]+", "-").Trim('-');
            if (slug.Length > 24) slug = slug.Substring(0, 24).Trim('-');
            if (slug.Length == 0) slug = "wallpaper";
            var rnd = new Random();
            string id;
            do id = slug + "-" + rnd.Next(0x1000, 0xFFFF).ToString("x"); while (Directory.Exists(Path.Combine(lib.LibDir, id)));
            return id;
        }

        // ---------- Microsoft Defender ----------

        static string MpCmdRun()
        {
            string platform = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                "Microsoft", "Windows Defender", "Platform");
            if (Directory.Exists(platform))
            {
                var newest = Directory.GetDirectories(platform)
                    .Where(d => File.Exists(Path.Combine(d, "MpCmdRun.exe")))
                    .OrderByDescending(d => d, StringComparer.OrdinalIgnoreCase).FirstOrDefault();
                if (newest != null) return Path.Combine(newest, "MpCmdRun.exe");
            }
            string pf = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Windows Defender", "MpCmdRun.exe");
            return File.Exists(pf) ? pf : null;
        }

        // "clean", "threat", or a short reason the scan couldn't run. Detected threats are handled (quarantined) by Defender.
        public static string Scan(string path)
        {
            string exe = MpCmdRun();
            if (exe == null) return "Defender not found";
            try
            {
                var psi = new ProcessStartInfo(exe, "-Scan -ScanType 3 -File \"" + path + "\"")
                {
                    CreateNoWindow = true, UseShellExecute = false, RedirectStandardOutput = true,
                };
                using (var p = Process.Start(psi))
                {
                    p.StandardOutput.ReadToEnd();
                    if (!p.WaitForExit(10 * 60 * 1000)) { p.Kill(); return "scan timed out"; }
                    if (p.ExitCode == 0) return "clean";
                    if (p.ExitCode == 2) return "threat";
                    return "scan error " + p.ExitCode;
                }
            }
            catch (Exception e) { return e.Message; }
        }
    }
}
