#include "viewport.h"

#include <math.h>

double fd_vp_world_left(const fd_viewport *vp)
{
    return vp->center_x - vp->width_px / 2.0 * vp->units_per_px;
}

double fd_vp_world_right(const fd_viewport *vp)
{
    return vp->center_x + vp->width_px / 2.0 * vp->units_per_px;
}

double fd_vp_world_bottom(const fd_viewport *vp)
{
    return vp->center_y - vp->height_px / 2.0 * vp->units_per_px;
}

double fd_vp_world_top(const fd_viewport *vp)
{
    return vp->center_y + vp->height_px / 2.0 * vp->units_per_px;
}

double fd_vp_world_width(const fd_viewport *vp)
{
    return vp->width_px * vp->units_per_px;
}

double fd_vp_world_height(const fd_viewport *vp)
{
    return vp->height_px * vp->units_per_px;
}

float fd_vp_to_screen_x(const fd_viewport *vp, double wx)
{
    return (float)((wx - vp->center_x) / vp->units_per_px + vp->width_px / 2.0);
}

float fd_vp_to_screen_y(const fd_viewport *vp, double wy)
{
    return (float)(vp->height_px / 2.0 - (wy - vp->center_y) / vp->units_per_px);
}

double fd_vp_to_world_x(const fd_viewport *vp, double sx)
{
    return (sx - vp->width_px / 2.0) * vp->units_per_px + vp->center_x;
}

double fd_vp_to_world_y(const fd_viewport *vp, double sy)
{
    return (vp->height_px / 2.0 - sy) * vp->units_per_px + vp->center_y;
}

void fd_vp_zoom(fd_viewport *vp, double factor, float fx, float fy)
{
    if (factor <= 0.0 || !isfinite(factor)) return;
    const double wx = fd_vp_to_world_x(vp, fx);
    const double wy = fd_vp_to_world_y(vp, fy);
    double next = vp->units_per_px / factor;
    if (next < FD_VP_MIN_UPP) next = FD_VP_MIN_UPP;
    if (next > FD_VP_MAX_UPP) next = FD_VP_MAX_UPP;
    if (next == vp->units_per_px) return;
    vp->center_x = wx - ((double)fx - vp->width_px / 2.0) * next;
    vp->center_y = wy + ((double)fy - vp->height_px / 2.0) * next;
    vp->units_per_px = next;
}

void fd_vp_pan(fd_viewport *vp, float dx, float dy)
{
    vp->center_x -= (double)dx * vp->units_per_px;
    vp->center_y += (double)dy * vp->units_per_px;
}

void fd_vp_resize(fd_viewport *vp, float w, float h)
{
    vp->width_px = w;
    vp->height_px = h;
}

double fd_vp_grid_step(const fd_viewport *vp)
{
    const double target = vp->units_per_px * 60.0;
    if (target <= 0.0 || !isfinite(target)) return 1.0;
    const double e = floor(log10(target));
    const double base = pow(10.0, e);
    const double norm = target / base;
    double mult;
    if (norm <= 1.5) mult = 1.0;
    else if (norm <= 3.5) mult = 2.0;
    else if (norm <= 7.5) mult = 5.0;
    else mult = 10.0;
    return mult * base;
}

fd_viewport fd_vp_for_range(float w, float h, double min_x, double max_x)
{
    fd_viewport vp = {w, h, (min_x + max_x) / 2.0, 0.0, 1.0};
    const double span = fabs(max_x - min_x);
    double upp = (w > 0.0f && span > 0.0) ? span / (double)w : 1.0;
    if (upp < FD_VP_MIN_UPP) upp = FD_VP_MIN_UPP;
    if (upp > FD_VP_MAX_UPP) upp = FD_VP_MAX_UPP;
    vp.units_per_px = upp;
    return vp;
}

fd_screen_delta fd_vp_delta_to(const fd_viewport *from, const fd_viewport *to)
{
    fd_screen_delta d = {1.0, 0.0, 0.0};
    if (from->units_per_px <= 0.0 || to->units_per_px <= 0.0) return d;

    d.k = from->units_per_px / to->units_per_px;
    const double w = to->width_px;
    const double h = to->height_px;
    d.tx = w / 2.0 * (1.0 - d.k) + (from->center_x - to->center_x) / to->units_per_px;
    d.ty = h / 2.0 * (1.0 - d.k) - (from->center_y - to->center_y) / to->units_per_px;
    return d;
}

bool fd_screen_delta_is_identity(const fd_screen_delta *d)
{
    return fabs(d->k - 1.0) < 1e-9 && fabs(d->tx) < 1e-6 && fabs(d->ty) < 1e-6;
}
