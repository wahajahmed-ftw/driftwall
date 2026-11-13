using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Linq;
using System.Windows.Forms;

namespace Driftwall
{
    static class Theme
    {
        public static readonly Color Background = Color.FromArgb(19, 19, 26);
        public static readonly Color Panel = Color.FromArgb(26, 26, 35);
        public static readonly Color Card = Color.FromArgb(33, 33, 44);
        public static readonly Color CardHover = Color.FromArgb(42, 42, 58);
        public static readonly Color Border = Color.FromArgb(52, 52, 68);
        public static readonly Color Accent = Color.FromArgb(99, 102, 241);
        public static readonly Color Accent2 = Color.FromArgb(34, 211, 238);
        public static readonly Color Text = Color.FromArgb(240, 240, 248);
        public static readonly Color Muted = Color.FromArgb(148, 148, 168);
        public static readonly Color Danger = Color.FromArgb(248, 113, 113);

        public static float Scale = 1f;
        public static int P(float v) { return (int)Math.Round(v * Scale); }

        public static Font Font(float px, bool semibold = false)
        {
            return new Font(semibold ? "Segoe UI Semibold" : "Segoe UI", px * Scale, GraphicsUnit.Pixel);
        }

        public static Font Icons(float px) { return new Font("Segoe Fluent Icons", px * Scale, GraphicsUnit.Pixel); }

        public static GraphicsPath Round(RectangleF r, float radius)
        {
            var p = new GraphicsPath();
            float d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
            if (d <= 0) { p.AddRectangle(r); return p; }
            p.AddArc(r.X, r.Y, d, d, 180, 90);
            p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            p.CloseFigure();
            return p;
        }

        public static void Smooth(Graphics g)
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = TextRenderingHint.ClearTypeGridFit;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
        }

        public static void DrawText(Graphics g, string s, Font f, Color c, RectangleF r, StringAlignment h = StringAlignment.Near,
                                    StringAlignment v = StringAlignment.Center)
        {
            using (var b = new SolidBrush(c))
            using (var fmt = new StringFormat { Alignment = h, LineAlignment = v, Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap })
                g.DrawString(s, f, b, r, fmt);
        }
    }

    // Base for owner-drawn, double-buffered controls with hover tracking.
    class PaintedControl : Control
    {
        protected bool Hover;
        public PaintedControl()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
            BackColor = Theme.Background;
        }
        protected override void OnMouseEnter(EventArgs e) { Hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { Hover = false; Invalidate(); base.OnMouseLeave(e); }
    }

    class DarkButton : PaintedControl
    {
        public string Glyph;
        public bool Accent, Selected;
        readonly Font font = Theme.Font(13, true), icon = Theme.Icons(14);

        public DarkButton(string text, string glyph = null, bool accent = false)
        {
            Text = text;
            Glyph = glyph;
            Accent = accent;
            Cursor = Cursors.Hand;
            Height = Theme.P(36);
            using (var g = CreateGraphics())
                Width = (int)g.MeasureString(text, font).Width + Theme.P(glyph != null ? 52 : 30);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Theme.Smooth(g);
            Color fill = Accent ? (Hover ? Color.FromArgb(118, 121, 247) : Theme.Accent)
                       : Selected ? Color.FromArgb(48, 48, 70) : Hover ? Theme.CardHover : Theme.Card;
            if (!Enabled) fill = Theme.Card;
            using (var path = Theme.Round(new RectangleF(0.5f, 0.5f, Width - 1, Height - 1), Theme.P(8)))
            using (var b = new SolidBrush(fill))
            {
                g.FillPath(b, path);
                if (!Accent)
                    using (var pen = new Pen(Selected ? Theme.Accent : Theme.Border)) g.DrawPath(pen, path);
            }
            Color fg = Enabled ? Theme.Text : Theme.Muted;
            float x = Theme.P(14);
            if (Glyph != null)
            {
                Theme.DrawText(g, Glyph, icon, fg, new RectangleF(x, 0, Theme.P(18), Height), StringAlignment.Center);
                x += Theme.P(24);
            }
            Theme.DrawText(g, Text, font, fg, new RectangleF(x, 0, Width - x - Theme.P(10), Height));
        }
    }

    // The monitors drawn in their real arrangement, each showing its current wallpaper. Click to select.
    class DisplayBar : PaintedControl
    {
        public List<MonitorInfo> Monitors = new List<MonitorInfo>();
        public Func<string, WallpaperItem> CurrentWallpaper = delegate { return null; };
        public string SelectedId;
        public event EventHandler SelectionChanged;
        RectangleF[] cards = new RectangleF[0];
        int hovered = -1;
        readonly Font numFont = Theme.Font(15, true), labelFont = Theme.Font(12), smallFont = Theme.Font(11, true);

        public DisplayBar() { Cursor = Cursors.Hand; }

        void LayoutCards()
        {
            cards = new RectangleF[Monitors.Count];
            if (Monitors.Count == 0) return;
            Rectangle all = Monitors[0].Bounds;
            foreach (var m in Monitors) all = Rectangle.Union(all, m.Bounds);
            float pad = Theme.P(16), areaW = Width - 2 * pad, areaH = Height - 2 * pad;
            float k = Math.Min(areaW / all.Width, areaH / all.Height);
            float ox = pad + (areaW - all.Width * k) / 2, oy = pad + (areaH - all.Height * k) / 2, gap = Theme.P(6);
            for (int i = 0; i < Monitors.Count; i++)
            {
                var b = Monitors[i].Bounds;
                cards[i] = new RectangleF(ox + (b.X - all.X) * k + gap, oy + (b.Y - all.Y) * k + gap,
                                          b.Width * k - 2 * gap, b.Height * k - 2 * gap);
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            LayoutCards();
            var g = e.Graphics;
            Theme.Smooth(g);
            for (int i = 0; i < cards.Length; i++)
            {
                var m = Monitors[i];
                var r = cards[i];
                bool sel = m.Id == SelectedId, hot = i == hovered;
                var wp = CurrentWallpaper(m.Id);
                using (var path = Theme.Round(r, Theme.P(10)))
                {
                    using (var b = new SolidBrush(Theme.Card)) g.FillPath(b, path);
                    if (wp != null && wp.Thumb != null)
                    {
                        var state = g.Save();
                        g.SetClip(path);
                        using (var fitted = Library.Cover(wp.Thumb, Math.Max(1, (int)r.Width), Math.Max(1, (int)r.Height)))
                            g.DrawImage(fitted, r.X, r.Y, r.Width, r.Height);
                        using (var shade = new LinearGradientBrush(r, Color.FromArgb(0, 0, 0, 0), Color.FromArgb(190, 0, 0, 0), 90f))
                            g.FillRectangle(shade, r);
                        g.Restore(state);
                    }
                    using (var pen = new Pen(sel ? Theme.Accent2 : hot ? Theme.Accent : Theme.Border, sel ? Theme.P(3) : Theme.P(1)))
                        g.DrawPath(pen, path);
                }
                // Number badge
                var badge = new RectangleF(r.X + Theme.P(10), r.Y + Theme.P(10), Theme.P(28), Theme.P(28));
                using (var bb = new SolidBrush(sel ? Theme.Accent2 : Color.FromArgb(200, 20, 20, 28))) g.FillEllipse(bb, badge);
                Theme.DrawText(g, m.Number.ToString(), numFont, sel ? Color.Black : Theme.Text, badge, StringAlignment.Center);
                if (m.Primary)
                    Theme.DrawText(g, "MAIN", smallFont, Theme.Accent2, new RectangleF(badge.Right + Theme.P(8), badge.Y, Theme.P(60), badge.Height));
                // Current wallpaper name
                string label = wp != null ? wp.Title : "No live wallpaper";
                Theme.DrawText(g, label, labelFont, Theme.Text,
                    new RectangleF(r.X + Theme.P(12), r.Bottom - Theme.P(30), r.Width - Theme.P(24), Theme.P(22)));
            }
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            int hit = -1;
            for (int i = 0; i < cards.Length; i++) if (cards[i].Contains(e.Location)) hit = i;
            if (hit != hovered) { hovered = hit; Invalidate(); }
        }

        protected override void OnMouseLeave(EventArgs e) { hovered = -1; base.OnMouseLeave(e); }

        protected override void OnMouseClick(MouseEventArgs e)
        {
            if (hovered < 0) return;
            SelectedId = Monitors[hovered].Id;
            Invalidate();
            if (SelectionChanged != null) SelectionChanged(this, EventArgs.Empty);
        }
    }

    class WallpaperTile : PaintedControl
    {
        public readonly WallpaperItem Item;
        public string InUse;  // e.g. "Display 1"
        readonly Font title = Theme.Font(14, true), sub = Theme.Font(12), badge = Theme.Font(11, true);

        public WallpaperTile(WallpaperItem item)
        {
            Item = item;
            Cursor = Cursors.Hand;
            Size = new Size(Theme.P(240), Theme.P(192));
            Margin = new Padding(Theme.P(8));
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Theme.Smooth(g);
            var r = new RectangleF(0.5f, 0.5f, Width - 1, Height - 1);
            float thumbH = Theme.P(135);
            using (var path = Theme.Round(r, Theme.P(10)))
            {
                using (var b = new SolidBrush(Hover ? Theme.CardHover : Theme.Card)) g.FillPath(b, path);
                var state = g.Save();
                g.SetClip(path);
                var tr = new RectangleF(0, 0, Width, thumbH);
                if (Item.Thumb != null) g.DrawImage(Item.Thumb, tr);
                else using (var b = new SolidBrush(Theme.Panel)) g.FillRectangle(b, tr);
                g.Restore(state);
                using (var pen = new Pen(InUse != null ? Theme.Accent2 : Hover ? Theme.Accent : Theme.Border, InUse != null ? Theme.P(2) : 1))
                    g.DrawPath(pen, path);
            }
            if (InUse != null)
            {
                var size = g.MeasureString(InUse, badge);
                var pill = new RectangleF(Theme.P(8), Theme.P(8), size.Width + Theme.P(12), Theme.P(22));
                using (var p = Theme.Round(pill, pill.Height / 2))
                using (var b = new SolidBrush(Theme.Accent2)) g.FillPath(b, p);
                Theme.DrawText(g, InUse, badge, Color.Black, pill, StringAlignment.Center);
            }
            Theme.DrawText(g, Item.Title, title, Theme.Text, new RectangleF(Theme.P(12), thumbH + Theme.P(8), Width - Theme.P(24), Theme.P(22)));
            Theme.DrawText(g, Item.TypeLabel, sub, Theme.Muted, new RectangleF(Theme.P(12), thumbH + Theme.P(30), Width - Theme.P(24), Theme.P(18)));
        }
    }

    class TileFlow : FlowLayoutPanel
    {
        public TileFlow() { DoubleBuffered = true; AutoScroll = true; BackColor = Theme.Background; }
        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            Native.SetWindowTheme(Handle, "DarkMode_Explorer", null);  // dark scrollbar
        }
    }

    class DarkMenuRenderer : ToolStripProfessionalRenderer
    {
        class Colors : ProfessionalColorTable
        {
            public override Color ToolStripDropDownBackground { get { return Theme.Card; } }
            public override Color MenuBorder { get { return Theme.Border; } }
            public override Color MenuItemSelected { get { return Theme.CardHover; } }
            public override Color MenuItemBorder { get { return Theme.CardHover; } }
            public override Color ImageMarginGradientBegin { get { return Theme.Card; } }
            public override Color ImageMarginGradientMiddle { get { return Theme.Card; } }
            public override Color ImageMarginGradientEnd { get { return Theme.Card; } }
            public override Color SeparatorDark { get { return Theme.Border; } }
            public override Color SeparatorLight { get { return Theme.Card; } }
        }
        public DarkMenuRenderer() : base(new Colors()) { }
        protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e)
        {
            e.TextColor = e.Item.Enabled ? (e.Item.Tag as string == "danger" ? Theme.Danger : Theme.Text) : Theme.Muted;
            base.OnRenderItemText(e);
        }

        public static ContextMenuStrip Menu()
        {
            return new ContextMenuStrip { Renderer = new DarkMenuRenderer(), Font = Theme.Font(13), ShowImageMargin = false };
        }
    }
}
