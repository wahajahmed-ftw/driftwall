using System;
using System.Drawing;
using System.Runtime.InteropServices;

namespace Driftwall
{
    [ComImport, Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IShellItemImageFactory
    {
        [PreserveSig] int GetImage(Native.SIZE size, int flags, out IntPtr phbm);
    }

    static class Native
    {
        public const uint WM_APP_RELOAD = 0x8000 + 1;  // WM_APP + 1, handled by the engine
        public const uint WM_APP_EXIT = 0x8000 + 2;

        // Segoe Fluent Icons code points used by the buttons.
        public const string GlyphAdd = "", GlyphSettings = "", GlyphStop = "", GlyphPlay = "",
            GlyphFolder = "";

        [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
        [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
        [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
        [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
        [DllImport("dwmapi.dll")] public static extern int DwmSetWindowAttribute(IntPtr h, int attr, ref int value, int size);
        [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)] public static extern int SetWindowTheme(IntPtr h, string app, string id);
        [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr h);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct DISPLAY_DEVICE
        {
            public int cb;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string DeviceName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceString;
            public int StateFlags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceID;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceKey;
        }
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern bool EnumDisplayDevices(string device, uint index, ref DISPLAY_DEVICE dd, uint flags);

        // Same ID the engine and IDesktopWallpaper use, e.g. \\?\DISPLAY#ABC1234#...#{e6f07b5f-...}
        public static string MonitorId(string deviceName)
        {
            var dd = new DISPLAY_DEVICE();
            dd.cb = Marshal.SizeOf(typeof(DISPLAY_DEVICE));
            if (EnumDisplayDevices(deviceName, 0, ref dd, 1) && !string.IsNullOrEmpty(dd.DeviceID)) return dd.DeviceID;
            return deviceName;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct SIZE { public int cx, cy; }

        [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
        static extern void SHCreateItemFromParsingName(string path, IntPtr pbc,
            [MarshalAs(UnmanagedType.LPStruct)] Guid riid, out IShellItemImageFactory item);

        // Windows' own thumbnail for a file (it can extract frames from videos). Must run on an STA thread.
        public static Bitmap ShellThumbnail(string path, int size)
        {
            IShellItemImageFactory factory;
            SHCreateItemFromParsingName(path, IntPtr.Zero, typeof(IShellItemImageFactory).GUID, out factory);
            IntPtr hbm;
            var sz = new SIZE { cx = size, cy = size };
            int hr = factory.GetImage(sz, 0x1 /* BIGGERSIZEOK */, out hbm);
            Marshal.ReleaseComObject(factory);
            if (hr != 0 || hbm == IntPtr.Zero) return null;
            try { return Image.FromHbitmap(hbm); }
            finally { DeleteObject(hbm); }
        }
    }
}
