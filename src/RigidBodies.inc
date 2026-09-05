/* C implementation included by Sandbox.c to share its particle/thermal state.
   The grid is the collision surface: bodies keep their own material masks and
   are rasterized into it between swept rigid-body steps. */
enum { BodyRasterCapacity = Sandbox_ShapeCells * 4 };
typedef struct BodyRaster {
    int count;
    int world[BodyRasterCapacity];
    uint16_t local[BodyRasterCapacity];
} BodyRaster;
typedef struct BodyContact {
    bool hit;
    int other, count;
    float x, y, nx, ny;
} BodyContact;

bool sandbox_material_is_rigid(Material m) {
    return (unsigned)m < Material_Count && properties[m].phase == Solid;
}
static float bodyDensity(Material m) {
    if (m == Material_Wood)
        return 0.45f;
    if (m == Material_Plant || m == Material_Moss)
        return 0.2f;
    if (m == Material_Ice || m == Material_Wax)
        return 0.9f;
    return fmaxf(properties[m].density / 40.0f, 0.2f);
}
static int bodySlot(const Sandbox* s, uint32_t id) {
    if (!id)
        return -1;
    for (int n = 0; n < Sandbox_MaxBodies; ++n)
        if (s->bodies[n].id == id)
            return n;
    return -1;
}
static int freeBodySlot(const Sandbox* s) {
    for (int n = 0; n < Sandbox_MaxBodies; ++n)
        if (!s->bodies[n].id)
            return n;
    return -1;
}
static uint32_t nextBodyId(Sandbox* s) {
    do {
        ++s->next_body_id;
    } while (!s->next_body_id || bodySlot(s, s->next_body_id) >= 0);
    return s->next_body_id;
}
static void bodyMass(RigidBody* b) {
    b->geometry_dirty = false;
    float mx = 0, my = 0;
    b->mass = 0;
    b->count = 0;
    b->min_x = b->min_y = Sandbox_ShapeSize;
    b->max_x = b->max_y = -1;
    for (int i = 0; i < Sandbox_ShapeCells; ++i)
        if (b->pixels[i].material) {
            int x = i % Sandbox_ShapeSize, y = i / Sandbox_ShapeSize;
            float mass = bodyDensity((Material)b->pixels[i].material);
            b->mass += mass;
            mx += x * mass;
            my += y * mass;
            ++b->count;
            if (x < b->min_x)
                b->min_x = x;
            if (y < b->min_y)
                b->min_y = y;
            if (x > b->max_x)
                b->max_x = x;
            if (y > b->max_y)
                b->max_y = y;
        }
    if (!b->count) {
        b->id = 0;
        return;
    }
    float px = mx / b->mass, py = my / b->mass;
    float c = cosf(b->angle), sn = sinf(b->angle);
    float dx = c * (px - b->pivot_x) - sn * (py - b->pivot_y);
    float dy = sn * (px - b->pivot_x) + c * (py - b->pivot_y);
    b->x += dx;
    b->y += dy;
    b->vx -= b->omega * dy;
    b->vy += b->omega * dx;
    b->pivot_x = px;
    b->pivot_y = py;
    b->inertia = 0;
    b->radius = 1;
    for (int i = 0; i < Sandbox_ShapeCells; ++i)
        if (b->pixels[i].material) {
            float rx = i % Sandbox_ShapeSize - px, ry = i / Sandbox_ShapeSize - py;
            float r2 = rx * rx + ry * ry;
            b->inertia += bodyDensity((Material)b->pixels[i].material) * (r2 + 1.0f / 6.0f);
            b->radius = fmaxf(b->radius, sqrtf(r2) + 1);
        }
}
static int bodyPixel(const RigidBody* b, float wx, float wy, float x, float y, float c, float sn) {
    /* Bias exact half-cell ties consistently. Float cancellation around a
       recentered COM can otherwise skip a whole one-pixel row at integer 32. */
    int lx = (int)floorf(c * (wx - x) + sn * (wy - y) + b->pivot_x + 0.5001f);
    int ly = (int)floorf(-sn * (wx - x) + c * (wy - y) + b->pivot_y + 0.5001f);
    if (lx < 0 || ly < 0 || lx >= Sandbox_ShapeSize || ly >= Sandbox_ShapeSize)
        return -1;
    int i = ly * Sandbox_ShapeSize + lx;
    return b->pixels[i].material ? i : -1;
}
static bool rigidObstacle(const Sandbox* s, int x, int y) {
    return !inBounds(x, y) ||
           sandbox_material_is_rigid((Material)s->cells[y * Sandbox_Width + x].material);
}
static bool rasterBody(const Sandbox* s, const RigidBody* b, float x, float y, float angle,
                       BodyRaster* raster, BodyContact* contact, bool placing) {
    float c = cosf(angle), sn = sinf(angle);
    float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
    for (int corner = 0; corner < 4; ++corner) {
        float lx = (corner & 1 ? b->max_x + 0.5f : b->min_x - 0.5f) - b->pivot_x;
        float ly = (corner & 2 ? b->max_y + 0.5f : b->min_y - 0.5f) - b->pivot_y;
        float wx = x + lx * c - ly * sn, wy = y + lx * sn + ly * c;
        left = fminf(left, wx);
        right = fmaxf(right, wx);
        top = fminf(top, wy);
        bottom = fmaxf(bottom, wy);
    }
    raster->count = 0;
    *contact = (BodyContact){0};
    contact->other = -1;
    for (int wy = (int)floorf(top); wy <= (int)ceilf(bottom); ++wy)
        for (int wx = (int)floorf(left); wx <= (int)ceilf(right); ++wx) {
            int local = bodyPixel(b, (float)wx, (float)wy, x, y, c, sn);
            if (local < 0)
                continue;
            bool inside = inBounds(wx, wy);
            int world = inside ? wy * Sandbox_Width + wx : -1;
            bool blocked = !inside || (placing ? s->cells[world].material != Material_Empty
                                               : rigidObstacle(s, wx, wy));
            if (blocked) {
                contact->hit = true;
                ++contact->count;
                contact->x += wx;
                contact->y += wy;
                float nx =
                    (float)rigidObstacle(s, wx - 1, wy) - (float)rigidObstacle(s, wx + 1, wy);
                float ny =
                    (float)rigidObstacle(s, wx, wy - 1) - (float)rigidObstacle(s, wx, wy + 1);
                if (wx < 0)
                    nx = 1;
                if (wx >= Sandbox_Width)
                    nx = -1;
                if (wy < 0)
                    ny = 1;
                if (wy >= Sandbox_Height)
                    ny = -1;
                contact->nx += nx;
                contact->ny += ny;
                if (inside && s->owner[world])
                    contact->other = s->owner[world] - 1;
            }
            if (inside && raster->count < BodyRasterCapacity) {
                int n = raster->count++;
                raster->world[n] = world;
                raster->local[n] = (uint16_t)local;
            }
        }
    if (contact->count) {
        contact->x /= contact->count;
        contact->y /= contact->count;
        float length = hypotf(contact->nx, contact->ny);
        if (length < 0.001f) {
            contact->nx = b->x - contact->x;
            contact->ny = b->y - contact->y;
            length = hypotf(contact->nx, contact->ny);
        }
        if (length > 0) {
            contact->nx /= length;
            contact->ny /= length;
        }
    }
    return !contact->hit && raster->count > 0;
}
static void removeBodyRaster(Sandbox* s, int slot, BodyRaster* raster) {
    raster->count = 0;
    const RigidBody* b = &s->bodies[slot];
    int left = (int)clamp(floorf(b->x - b->radius - 2), 0, Sandbox_Width - 1);
    int right = (int)clamp(ceilf(b->x + b->radius + 2), 0, Sandbox_Width - 1);
    int top = (int)clamp(floorf(b->y - b->radius - 2), 0, Sandbox_Height - 1);
    int bottom = (int)clamp(ceilf(b->y + b->radius + 2), 0, Sandbox_Height - 1);
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            int i = y * Sandbox_Width + x;
            if (s->owner[i] != slot + 1)
                continue;
            int n = raster->count++;
            raster->world[n] = i;
            raster->local[n] = s->local_pixel[i];
            s->owner[i] = 0;
            s->cells[i] = makeCell(Material_Empty);
        }
}
static void writeBodyRaster(Sandbox* s, int slot, const BodyRaster* raster) {
    RigidBody* b = &s->bodies[slot];
    for (int n = 0; n < raster->count; ++n) {
        int i = raster->world[n], local = raster->local[n];
        s->cells[i] = b->pixels[local];
        s->cells[i].vx = b->vx - b->omega * (i / Sandbox_Width - b->y);
        s->cells[i].vy = b->vy + b->omega * (i % Sandbox_Width - b->x);
        s->owner[i] = (uint16_t)(slot + 1);
        s->local_pixel[i] = (uint16_t)local;
    }
}
static bool displaceParticles(Sandbox* s, const BodyRaster* next, const BodyRaster* old) {
    /* Reserve every destination before moving anything. No fluid disappears if
       the container is full: the body simply cannot enter that position. */
    int targets[BodyRasterCapacity], vacant = 0;
    bool fits = true;
    for (int n = 0; n < next->count; ++n) {
        s->body_mask[next->world[n]] = 1;
        targets[n] = -1;
    }
    for (int n = 0; n < next->count; ++n) {
        int i = next->world[n];
        targets[n] = -1;
        if (!s->cells[i].material)
            continue;
        while (vacant < old->count) {
            int candidate = old->world[vacant++];
            if (!s->body_mask[candidate] && !s->displacement_mask[candidate] &&
                !s->cells[candidate].material) {
                targets[n] = candidate;
                break;
            }
        }
        int x = i % Sandbox_Width, y = i / Sandbox_Width;
        for (int radius = 1; radius <= Sandbox_ShapeSize && targets[n] < 0; ++radius)
            for (int dy = -radius; dy <= radius && targets[n] < 0; ++dy)
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (abs(dx) != radius && abs(dy) != radius)
                        continue;
                    if (!inBounds(x + dx, y + dy))
                        continue;
                    int candidate = (y + dy) * Sandbox_Width + x + dx;
                    if (!s->body_mask[candidate] && !s->displacement_mask[candidate] &&
                        !s->cells[candidate].material) {
                        targets[n] = candidate;
                        break;
                    }
                }
        if (targets[n] < 0) {
            fits = false;
            break;
        }
        s->displacement_mask[targets[n]] = 1;
    }
    for (int n = 0; n < next->count; ++n) {
        s->body_mask[next->world[n]] = 0;
        if (targets[n] >= 0) {
            s->displacement_mask[targets[n]] = 0;
            if (fits) {
                s->cells[targets[n]] = s->cells[next->world[n]];
                s->cells[next->world[n]] = makeCell(Material_Empty);
            }
        }
    }
    return fits;
}
static int labelComponents(const RigidBody* b, uint16_t* labels) {
    int queue[Sandbox_ShapeCells], components = 0;
    memset(labels, 0, Sandbox_ShapeCells * sizeof(*labels));
    for (int seed = 0; seed < Sandbox_ShapeCells; ++seed) {
        if (!b->pixels[seed].material || labels[seed])
            continue;
        ++components;
        int begin = 0, end = 0;
        queue[end++] = seed;
        labels[seed] = (uint16_t)components;
        while (begin < end) {
            int i = queue[begin++], x = i % Sandbox_ShapeSize, y = i / Sandbox_ShapeSize;
            const int neighbors[] = {x ? i - 1 : -1, x + 1 < Sandbox_ShapeSize ? i + 1 : -1,
                                     y ? i - Sandbox_ShapeSize : -1,
                                     y + 1 < Sandbox_ShapeSize ? i + Sandbox_ShapeSize : -1};
            for (int n = 0; n < 4; ++n) {
                int next = neighbors[n];
                if (next < 0 || labels[next] || !b->pixels[next].material)
                    continue;
                labels[next] = (uint16_t)components;
                queue[end++] = next;
            }
        }
    }
    return components;
}
static bool prepareShape(RigidBody* body, const SandboxShape* shape, float x, float y,
                         float angle) {
    if (!shape || !isfinite(x) || !isfinite(y) || !isfinite(angle) || x < 0 || y < 0 ||
        x >= Sandbox_Width || y >= Sandbox_Height)
        return false;
    memset(body, 0, sizeof(*body));
    if (!isfinite(shape->break_speed) || shape->break_speed < 0)
        return false;
    body->break_speed = shape->break_speed;
    body->x = x;
    body->y = y;
    body->angle = remainderf(angle, 6.2831853f);
    body->pivot_x = body->pivot_y = (Sandbox_ShapeSize - 1) * 0.5f;
    for (int i = 0; i < Sandbox_ShapeCells; ++i) {
        Material m = (Material)shape->pixels[i];
        if (m && !sandbox_material_is_rigid(m))
            return false;
        body->pixels[i] = makeCell(m);
        body->pixels[i].tint = shape->colors[i] & UINT32_C(0x1FFFFFF);
    }
    bodyMass(body);
    uint16_t labels[Sandbox_ShapeCells];
    return body->count > 0 && labelComponents(body, labels) == 1;
}
bool sandbox_can_place_shape(const Sandbox* s, const SandboxShape* shape, float x, float y,
                             float angle) {
    RigidBody body;
    BodyRaster raster;
    BodyContact contact;
    return freeBodySlot(s) >= 0 && prepareShape(&body, shape, x, y, angle) &&
           rasterBody(s, &body, body.x, body.y, body.angle, &raster, &contact, true);
}
uint32_t sandbox_create_body(Sandbox* s, const SandboxShape* shape, float x, float y, float angle,
                             bool fixed) {
    int slot = freeBodySlot(s);
    RigidBody body;
    BodyRaster raster;
    BodyContact contact;
    if (slot < 0 || !prepareShape(&body, shape, x, y, angle) ||
        !rasterBody(s, &body, body.x, body.y, body.angle, &raster, &contact, true))
        return 0;
    body.id = nextBodyId(s);
    body.fixed = fixed;
    s->bodies[slot] = body;
    writeBodyRaster(s, slot, &raster);
    return body.id;
}
int sandbox_body_count(const Sandbox* s) {
    int count = 0;
    for (int n = 0; n < Sandbox_MaxBodies; ++n)
        if (s->bodies[n].id)
            ++count;
    return count;
}
uint32_t sandbox_body_at(const Sandbox* s, int x, int y) {
    if (!inBounds(x, y))
        return 0;
    int owner = s->owner[y * Sandbox_Width + x];
    return owner ? s->bodies[owner - 1].id : 0;
}
bool sandbox_body_info(const Sandbox* s, uint32_t id, SandboxBodyInfo* info) {
    int slot = bodySlot(s, id);
    if (slot < 0 || !info)
        return false;
    const RigidBody* b = &s->bodies[slot];
    *info =
        (SandboxBodyInfo){b->id, b->x, b->y, b->angle, b->vx, b->vy, b->omega, b->count, b->fixed};
    return true;
}
static void applyBodyImpulse(RigidBody* b, float x, float y, float ix, float iy) {
    if (b->fixed || !b->id)
        return;
    b->vx = clamp(b->vx + ix / b->mass, -6, 6);
    b->vy = clamp(b->vy + iy / b->mass, -6, 6);
    b->omega = clamp(b->omega + ((x - b->x) * iy - (y - b->y) * ix) / b->inertia, -0.12f, 0.12f);
}
void sandbox_body_impulse(Sandbox* s, uint32_t id, float x, float y, float ix, float iy) {
    int slot = bodySlot(s, id);
    if (slot >= 0 && isfinite(x) && isfinite(y) && isfinite(ix) && isfinite(iy) &&
        fabsf(x) < 10000 && fabsf(y) < 10000 && fabsf(ix) < 1e8f && fabsf(iy) < 1e8f) {
        RigidBody* b = &s->bodies[slot];
        applyBodyImpulse(b, x, y, ix, iy);
        if (b->break_speed > 0 && hypotf(b->vx, b->vy) + fabsf(b->omega) * b->radius > 1.5f)
            b->throw_armed = true;
    }
}
void sandbox_body_grab(Sandbox* s, uint32_t id, float x, float y) {
    int slot = bodySlot(s, id);
    if (slot < 0 || !isfinite(x) || !isfinite(y)) {
        int held = bodySlot(s, s->grabbed_body);
        if (held >= 0) {
            RigidBody* b = &s->bodies[held];
            if (b->break_speed > 0 && hypotf(b->vx, b->vy) + fabsf(b->omega) * b->radius > 1.5f)
                b->throw_armed = true;
        }
        s->grabbed_body = 0;
        return;
    }
    RigidBody* b = &s->bodies[slot];
    x = clamp(x, 0, Sandbox_Width - 1);
    y = clamp(y, 0, Sandbox_Height - 1);
    if (s->grabbed_body != id) {
        b->throw_armed = false;
        float c = cosf(b->angle), sn = sinf(b->angle);
        s->grab_local_x = c * (x - b->x) + sn * (y - b->y);
        s->grab_local_y = -sn * (x - b->x) + c * (y - b->y);
    }
    s->grabbed_body = id;
    s->grab_x = x;
    s->grab_y = y;
}
static void bodyBlast(Sandbox* s, int x, int y, int radius) {
    for (int slot = 0; slot < Sandbox_MaxBodies; ++slot) {
        RigidBody* b = &s->bodies[slot];
        if (!b->id || b->fixed || hypotf(b->x - x, b->y - y) > radius + b->radius)
            continue;
        float c = cosf(b->angle), sn = sinf(b->angle), ix = 0, iy = 0, torque = 0;
        for (int i = 0; i < Sandbox_ShapeCells; ++i)
            if (b->pixels[i].material) {
                float lx = i % Sandbox_ShapeSize - b->pivot_x,
                      ly = i / Sandbox_ShapeSize - b->pivot_y;
                float rx = c * lx - sn * ly, ry = sn * lx + c * ly;
                float dx = b->x + rx - x, dy = b->y + ry - y, distance = hypotf(dx, dy);
                if (distance < 0.01f || distance >= radius)
                    continue;
                float strength = (1 - distance / radius) * 4 *
                                 bodyDensity((Material)b->pixels[i].material) / distance;
                float fx = dx * strength, fy = dy * strength;
                ix += fx;
                iy += fy;
                torque += rx * fy - ry * fx;
            }
        applyBodyImpulse(b, b->x, b->y, ix, iy);
        b->omega = clamp(b->omega + torque / b->inertia, -0.12f, 0.12f);
    }
}
static void syncBodyPixels(Sandbox* s) {
    for (int i = 0; i < CellCount; ++i)
        if (s->owner[i]) {
            RigidBody* b = &s->bodies[s->owner[i] - 1];
            Cell* pixel = &b->pixels[s->local_pixel[i]];
            if (!sandbox_material_is_rigid((Material)s->cells[i].material)) {
                b->geometry_dirty = true;
                *pixel = makeCell(Material_Empty);
                s->owner[i] = 0;
            } else if (pixel->material) {
                if (pixel->material != s->cells[i].material)
                    b->geometry_dirty = true;
                *pixel = s->cells[i];
            }
        }
    /* A rotated source pixel can cover more than one grid cell. Damage to any
       of those cells removes it consistently; freed liquid/gas stays simulated. */
    for (int i = 0; i < CellCount; ++i)
        if (s->owner[i] && !s->bodies[s->owner[i] - 1].pixels[s->local_pixel[i]].material) {
            s->owner[i] = 0;
            s->cells[i] = makeCell(Material_Empty);
        }
}
static void splitBody(Sandbox* s, int slot) {
    RigidBody* b = &s->bodies[slot];
    uint16_t labels[Sandbox_ShapeCells];
    int count = labelComponents(b, labels);
    if (count <= 1) {
        bodyMass(b);
        return;
    }
    int slots[Sandbox_ShapeCells + 1];
    slots[1] = slot;
    for (int group = 2; group <= count; ++group) {
        int next = freeBodySlot(s);
        slots[group] = next;
        if (next < 0)
            continue;
        RigidBody* fragment = &s->bodies[next];
        *fragment = *b;
        fragment->id = nextBodyId(s);
        fragment->fixed = false;
        for (int i = 0; i < Sandbox_ShapeCells; ++i)
            if (labels[i] != group)
                fragment->pixels[i] = makeCell(Material_Empty);
        bodyMass(fragment);
    }
    for (int i = 0; i < CellCount; ++i)
        if (s->owner[i] == slot + 1) {
            int group = labels[s->local_pixel[i]], target = slots[group];
            if (target < 0) {
                s->owner[i] = 0;
                changeMaterial(&s->cells[i], debris((Material)s->cells[i].material));
            } else
                s->owner[i] = (uint16_t)(target + 1);
        }
    for (int i = 0; i < Sandbox_ShapeCells; ++i)
        if (labels[i] != 1)
            b->pixels[i] = makeCell(Material_Empty);
    bodyMass(b);
}
static void recordImpact(RigidBody* b, float speed, float x, float y, float nx, float ny) {
    if (!b || b->fixed || !b->throw_armed || b->break_speed <= 0 || speed < b->break_speed ||
        speed <= b->impact_speed)
        return;
    b->impact_speed = speed;
    b->impact_x = x;
    b->impact_y = y;
    b->impact_nx = nx;
    b->impact_ny = ny;
    b->impact_vx = b->vx;
    b->impact_vy = b->vy;
    b->impact_omega = b->omega;
}
static void fractureImpact(Sandbox* s, int slot) {
    RigidBody* b = &s->bodies[slot];
    float speed = b->impact_speed;
    if (!b->id || speed <= 0)
        return;
    b->impact_speed = 0;
    b->throw_armed = false;
    if (s->grabbed_body == b->id)
        s->grabbed_body = 0;
    int seeds[8], seed_count = 0;
    uint8_t region[Sandbox_ShapeCells] = {0}, crack[Sandbox_ShapeCells] = {0};
    int desired = (int)clamp(4 + (speed - b->break_speed), 4, 8);
    float c = cosf(b->angle), sn = sinf(b->angle);
    float hit_x = c * (b->impact_x - b->x) + sn * (b->impact_y - b->y) + b->pivot_x;
    float hit_y = -sn * (b->impact_x - b->x) + c * (b->impact_y - b->y) + b->pivot_y;
    /* Deterministic farthest-point seeds partition the material mask into
       irregular chunks. Boundary pixels become freely simulated debris. */
    for (int n = 0; n < desired; ++n) {
        float best = -1e30f;
        int chosen = -1;
        for (int i = 0; i < Sandbox_ShapeCells; ++i)
            if (b->pixels[i].material) {
                float hx = i % 48 - hit_x, hy = i / 48 - hit_y;
                float distance = seed_count ? 1e9f : -(hx * hx + hy * hy);
                for (int j = 0; j < seed_count; ++j) {
                    float dx = (float)(i % 48 - seeds[j] % 48),
                          dy = (float)(i / 48 - seeds[j] / 48);
                    distance = fminf(distance, dx * dx + dy * dy);
                }
                if (distance > best) {
                    best = distance;
                    chosen = i;
                }
            }
        if (chosen < 0 || (seed_count && best < 9))
            break;
        seeds[seed_count++] = chosen;
    }
    for (int i = 0; i < Sandbox_ShapeCells; ++i)
        if (b->pixels[i].material) {
            float best = 1e9f;
            for (int n = 0; n < seed_count; ++n) {
                float dx = (float)(i % 48 - seeds[n] % 48), dy = (float)(i / 48 - seeds[n] / 48);
                float distance = dx * dx + dy * dy;
                if (distance < best) {
                    best = distance;
                    region[i] = (uint8_t)(n + 1);
                }
            }
        }
    for (int i = 0; i < Sandbox_ShapeCells; ++i)
        if (region[i]) {
            int x = i % 48, y = i / 48;
            const int neighbors[] = {x ? i - 1 : -1, x < 47 ? i + 1 : -1, y ? i - 48 : -1,
                                     y < 47 ? i + 48 : -1};
            crack[i] = (uint8_t)(b->count < 24);
            for (int n = 0; n < 4; ++n) {
                int j = neighbors[n];
                if (j >= 0 && region[j] && region[j] != region[i])
                    crack[i] = 1;
            }
        }
    float vx = b->impact_vx * 0.3f + b->impact_nx * speed * 0.6f;
    float vy = b->impact_vy * 0.3f + b->impact_ny * speed * 0.6f;
    for (int i = 0; i < CellCount; ++i)
        if (s->owner[i] == slot + 1 && crack[s->local_pixel[i]]) {
            Cell* cell = &s->cells[i];
            uint32_t tint = cell->tint;
            changeMaterial(cell, debris((Material)cell->material));
            cell->tint = tint;
            float dx = i % Sandbox_Width - b->x, dy = i / Sandbox_Width - b->y,
                  length = fmaxf(hypotf(dx, dy), 1);
            cell->vx = clamp(vx - b->impact_omega * dy + dx / length, -6, 6);
            cell->vy = clamp(vy + b->impact_omega * dx + dy / length, -6, 6);
            s->owner[i] = 0;
        }
    for (int i = 0; i < Sandbox_ShapeCells; ++i)
        if (crack[i])
            b->pixels[i] = makeCell(Material_Empty);
    b->vx = vx;
    b->vy = vy;
    b->omega = b->impact_omega * 0.5f;
    b->break_speed *= 1.2f;
    float cx = b->x, cy = b->y;
    uint32_t previous[Sandbox_MaxBodies];
    for (int n = 0; n < Sandbox_MaxBodies; ++n)
        previous[n] = s->bodies[n].id;
    splitBody(s, slot);
    for (int n = 0; n < Sandbox_MaxBodies; ++n) {
        RigidBody* fragment = &s->bodies[n];
        if (fragment->id && (n == slot || !previous[n])) {
            float dx = fragment->x - cx, dy = fragment->y - cy, length = fmaxf(hypotf(dx, dy), 1);
            applyBodyImpulse(fragment, fragment->x, fragment->y,
                             dx / length * fragment->mass * 0.8f,
                             dy / length * fragment->mass * 0.8f);
        }
    }
}
static void resolveBodyContact(Sandbox* s, RigidBody* b, const BodyContact* contact) {
    RigidBody* other = contact->other >= 0 ? &s->bodies[contact->other] : NULL;
    float nx = contact->nx, ny = contact->ny;
    float rx = contact->x - b->x, ry = contact->y - b->y;
    float ox = other ? contact->x - other->x : 0, oy = other ? contact->y - other->y : 0;
    float ovx = other && !other->fixed ? other->vx - other->omega * oy : 0;
    float ovy = other && !other->fixed ? other->vy + other->omega * ox : 0;
    float vx = b->vx - b->omega * ry - ovx, vy = b->vy + b->omega * rx - ovy;
    float normalSpeed = vx * nx + vy * ny;
    if (normalSpeed >= 0)
        return;
    recordImpact(b, -normalSpeed, contact->x, contact->y, nx, ny);
    recordImpact(other, -normalSpeed, contact->x, contact->y, -nx, -ny);
    float ra = rx * ny - ry * nx, rb = ox * ny - oy * nx;
    float invOther = other && !other->fixed ? 1 / other->mass : 0;
    float invInertia = other && !other->fixed ? 1 / other->inertia : 0;
    float impulse = -(1.04f) * normalSpeed /
                    (1 / b->mass + invOther + ra * ra / b->inertia + rb * rb * invInertia);
    float tx = -ny, ty = nx;
    float ta = rx * ty - ry * tx, tb = ox * ty - oy * tx;
    float friction = -(vx * tx + vy * ty) /
                     (1 / b->mass + invOther + ta * ta / b->inertia + tb * tb * invInertia);
    /* Side contacts must not act like a shelf. Keep support friction on floors,
       but allow gravity/tangential motion along walls and neighboring bodies. */
    float coefficient = fabsf(ny) > 0.65f ? 0.4f : 0.03f;
    if (contact->x < 0 || contact->x >= Sandbox_Width || contact->y < 0)
        coefficient = 0;
    friction = clamp(friction, -impulse * coefficient, impulse * coefficient);
    float ix = nx * impulse + tx * friction, iy = ny * impulse + ty * friction;
    applyBodyImpulse(b, contact->x, contact->y, ix, iy);
    if (other)
        applyBodyImpulse(other, contact->x, contact->y, -ix, -iy);
}
static bool moveBodyTo(Sandbox* s, int slot, float x, float y, float angle, BodyRaster* old,
                       BodyRaster* next, BodyContact* contact) {
    RigidBody* b = &s->bodies[slot];
    removeBodyRaster(s, slot, old);
    if (rasterBody(s, b, x, y, angle, next, contact, false) && displaceParticles(s, next, old)) {
        b->x = x;
        b->y = y;
        b->angle = angle;
        writeBodyRaster(s, slot, next);
        return true;
    }
    writeBodyRaster(s, slot, old);
    return false;
}
static bool slideBodyTo(Sandbox* s, int slot, float dx, float dy, BodyRaster* old, BodyRaster* next,
                        BodyContact* contact) {
    RigidBody* b = &s->bodies[slot];
    if (moveBodyTo(s, slot, b->x + dx, b->y + dy, b->angle, old, next, contact))
        return true;
    BodyContact original = *contact;
    /* Rotated raster edges can interlock by a fraction of a pixel. A small
       outward skin permits tangential motion; every candidate is collision tested. */
    float distance = hypotf(dx, dy);
    if (contact->hit && fabsf(dx * contact->nx + dy * contact->ny) < distance * 0.35f) {
        for (int n = 1; n <= 4; ++n) {
            float skin = n * 0.125f;
            if (moveBodyTo(s, slot, b->x + dx + original.nx * skin, b->y + dy + original.ny * skin,
                           b->angle, old, next, contact))
                return true;
        }
    }
    *contact = original;
    return false;
}
static void updateBodies(Sandbox* s) {
    if (!sandbox_body_count(s))
        return;
    syncBodyPixels(s);
    for (int slot = 0; slot < Sandbox_MaxBodies; ++slot)
        if (s->bodies[slot].id && s->bodies[slot].geometry_dirty)
            splitBody(s, slot);
    /* Collect every body's occupied rows in one grid pass, rather than scanning
       all 57,600 cells separately for each object's buoyancy calculation. */
    memset(s->body_rows, 0, sizeof(s->body_rows));
    for (int y = 0; y < Sandbox_Height; ++y)
        for (int x = 0; x < Sandbox_Width; ++x) {
            int owner = s->owner[y * Sandbox_Width + x];
            if (!owner)
                continue;
            int slot = owner - 1;
            if (!s->body_rows[slot][y].count)
                s->body_rows[slot][y].left = (uint16_t)x;
            s->body_rows[slot][y].right = (uint16_t)x;
            ++s->body_rows[slot][y].count;
        }
    for (int slot = 0; slot < Sandbox_MaxBodies; ++slot) {
        RigidBody* b = &s->bodies[slot];
        if (!b->id || b->fixed)
            continue;
        b->vx *= 0.998f;
        b->vy = clamp(b->vy + 0.16f, -6, 6);
        b->omega *= 0.995f;
        /* Estimate displaced volume from fluid at each occupied row's sides. */
        float displaced = 0;
        for (int y = 0; y < Sandbox_Height; ++y) {
            int pixels = s->body_rows[slot][y].count;
            if (!pixels)
                continue;
            int left = s->body_rows[slot][y].left, right = s->body_rows[slot][y].right;
            float density = 0;
            for (int side = 0; side < 2; ++side) {
                int x = side ? right + 1 : left - 1;
                if (!pixels || !inBounds(x, y))
                    continue;
                Material m = (Material)s->cells[y * Sandbox_Width + x].material;
                if (properties[m].phase == Liquid)
                    density = fmaxf(density, properties[m].density / 40.0f);
            }
            displaced += pixels * density;
        }
        if (displaced > 0) {
            b->vy = clamp(b->vy - 0.16f * displaced / b->mass, -6, 6);
            b->vx *= 0.97f;
            b->vy *= 0.97f;
            b->omega *= 0.94f;
        }
        if (s->grabbed_body == b->id) {
            float c = cosf(b->angle), sn = sinf(b->angle);
            float rx = c * s->grab_local_x - sn * s->grab_local_y;
            float ry = sn * s->grab_local_x + c * s->grab_local_y;
            float fx = (s->grab_x - b->x - rx) * 0.06f - (b->vx - b->omega * ry) * 0.3f;
            float fy = (s->grab_y - b->y - ry) * 0.06f - (b->vy + b->omega * rx) * 0.3f;
            applyBodyImpulse(b, b->x + rx, b->y + ry, fx * b->mass, fy * b->mass);
        }
        int steps = (int)ceilf((hypotf(b->vx, b->vy) + fabsf(b->omega) * b->radius) / 0.35f);
        if (steps < 1)
            steps = 1;
        if (steps > 64)
            steps = 64;
        BodyRaster old, next;
        BodyContact contact;
        for (int n = 0; n < steps; ++n) {
            float dx = b->vx / steps, dy = b->vy / steps, da = b->omega / steps;
            if (moveBodyTo(s, slot, b->x + dx, b->y + dy, remainderf(b->angle + da, 6.2831853f),
                           &old, &next, &contact))
                continue;
            /* A blocked rotation or horizontal component must not cancel free
               vertical motion (and vice versa). Each accepted component still
               sweeps less than one cell and independently displaces particles. */
            if (fabsf(dx) > 0.000001f && !slideBodyTo(s, slot, dx, 0, &old, &next, &contact)) {
                if (contact.hit)
                    resolveBodyContact(s, b, &contact);
                else
                    b->vx *= 0.5f;
            }
            if (fabsf(dy) > 0.000001f && !slideBodyTo(s, slot, 0, dy, &old, &next, &contact)) {
                if (contact.hit)
                    resolveBodyContact(s, b, &contact);
                else
                    b->vy *= 0.5f;
            }
            if (fabsf(da) > 0.000001f &&
                !moveBodyTo(s, slot, b->x, b->y, remainderf(b->angle + da, 6.2831853f), &old, &next,
                            &contact)) {
                if (contact.hit)
                    resolveBodyContact(s, b, &contact);
                b->omega *= 0.5f;
            }
        }
    }
    /* Defer topology changes until no collision solver holds body references. */
    for (int slot = 0; slot < Sandbox_MaxBodies; ++slot)
        fractureImpact(s, slot);
}
