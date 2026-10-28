#!/usr/bin/env python3
"""Regenerates the platformer slice's placeholder art, sounds, scene and project.json.

Only needed when changing the level layout; the generated files are committed.
Usage: python3 -I make_assets.py   (run from samples/platformer_slice)
"""
import json, math, struct, wave, zlib, os

def fnv1a(s):
    h = 0xcbf29ce484222325
    for c in s.encode():
        h ^= c
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

def write_png(path, w, h, px):  # px[y][x] = (r,g,b,a)
    raw = b"".join(b"\x00" + b"".join(bytes(p) for p in row) for row in px)
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)

def sheet(w, h, fn):
    return [[fn(x, y) for x in range(w)] for y in range(h)]

A = "assets/"
# tiles.png: 1 grass top, 2 dirt, 3 brick platform (16x16 each)
def tile(x, y):
    t, lx = x // 16, x % 16
    if t == 0: return (70, 170, 70, 255) if y < 5 else (120, 80, 50, 255)
    if t == 1: return (120, 80, 50, 255) if (x + y) % 7 else (95, 62, 38, 255)
    return (170, 90, 60, 255) if (y % 8 and lx % 8) else (90, 50, 35, 255)
write_png(A + "tiles.png", 48, 16, sheet(48, 16, tile))
# hero.png: 4 frames of 16x24; 0-1 idle (blink), 2-3 run (legs alternate)
def hero(x, y):
    f, lx = x // 16, x % 16
    if y < 8: return (240, 200, 160, 255) if 3 <= lx <= 12 else (0, 0, 0, 0)
    if y < 18: return (60, 90, 220, 255) if 2 <= lx <= 13 else (0, 0, 0, 0)
    leg = (lx < 7) if f in (0, 2) else (lx > 8)
    if f < 2: leg = lx < 7 or lx > 8
    return (50, 50, 60, 255) if leg and 2 <= lx <= 13 else (0, 0, 0, 0)
write_png(A + "hero.png", 64, 24, sheet(64, 24, hero))
def coin(x, y):
    return (250, 210, 40, 255) if (x - 7.5) ** 2 + (y - 7.5) ** 2 <= 49 else (0, 0, 0, 0)
write_png(A + "coin.png", 16, 16, sheet(16, 16, coin))

def beep(name, freq, secs):
    w = wave.open(A + name, "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
    w.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * freq * i / 22050) * (1 - i / (22050 * secs))))
                           for i in range(int(22050 * secs))))
    w.close()
beep("coin.wav", 880, 0.12); beep("hurt.wav", 180, 0.25); beep("win.wav", 1320, 0.4)

def clip(name, frames, dur):
    return {"format_version": 1, "name": name, "loop": True,
            "frames": [{"uv": [f / 4, 0.0, (f + 1) / 4, 1.0], "duration_seconds": dur} for f in frames]}
json.dump(clip("idle", [0, 1], 0.5), open(A + "hero_idle.json", "w"), indent=2)
json.dump(clip("run", [2, 3], 0.1), open(A + "hero_run.json", "w"), indent=2)
json.dump({"format_version": 1, "texture": fnv1a("tiles.png"), "tile_width": 16, "tile_height": 16,
           "columns": 3, "tile_count": 3}, open(A + "tiles_set.json", "w"), indent=2)

# ---- scene: everything under "Level" so the module's SliceRules can reach it via node children
nodes, nid = [], [0]
def nxt():
    nid[0] += 1; return nid[0]
def node(name, pos, parent, comps, scale=(1.0, 1.0)):
    i = nxt()
    nodes.append({"id": i, "name": name, "parent_id": parent, "position": list(pos), "rotation_radians": 0.0,
                  "scale": list(scale), "components": [{"id": nxt(), "type": t, "properties": p} for t, p in comps]})
    return i
white = [1.0, 1.0, 1.0, 1.0]
level = node("Level", (0, 0), 0, [("SliceRules", {
    "idle_clip": fnv1a("hero_idle.json"), "run_clip": fnv1a("hero_run.json"),
    "win_x": 1300.0, "pit_y": 700.0, "coin_value": 10, "hurt_cooldown": 1.0})])
node("MainCamera", (400, 300), level, [("Camera", {"zoom": 1.0, "primary": True})])
node("Player", (64, 340), level, [
    ("Sprite", {"texture": fnv1a("hero.png"), "width": 32.0, "height": 48.0, "tint": white}),
    ("AnimatedSprite", {"clip": fnv1a("hero_idle.json"), "speed": 1.0, "playing": True}),
    ("RigidBody2D", {"body_type": 2, "linear_velocity": [0.0, 0.0], "gravity_scale": 1.0, "fixed_rotation": True}),
    ("Collider2D", {"shape_type": 0, "width": 28.0, "height": 46.0, "radius": 16.0, "offset": [0.0, 1.0],
                    "density": 1.0, "friction": 0.0, "restitution": 0.0, "is_sensor": False}),
    ("PlatformerController2D", {"max_speed": 200.0, "run_acceleration": 1400.0, "run_deceleration": 1800.0,
        "air_control": 0.6, "jump_speed": 420.0, "fall_gravity_scale": 1.6, "jump_cut": 0.5, "max_fall_speed": 700.0,
        "coyote_time": 0.1, "jump_buffer_time": 0.1, "left_action": "move_left", "right_action": "move_right",
        "jump_action": "jump"})])
W, H = 40, 2
cells = [[0] * W for _ in range(H)]
for x in range(W):
    if 20 <= x <= 22: continue  # pit
    cells[0][x] = 1; cells[1][x] = 2
node("Ground", (0, 400), level, [("Tilemap", {
    "tileset": fnv1a("tiles_set.json"), "tile_width": 32.0, "tile_height": 32.0, "width": W, "height": H,
    "collide": True, "solid_tiles": "1,2,3", "layers": ",".join(str(c) for r in cells for c in r)})])
node("Platform", (400, 300), level, [("Tilemap", {
    "tileset": fnv1a("tiles_set.json"), "tile_width": 32.0, "tile_height": 32.0, "width": 4, "height": 1,
    "collide": True, "solid_tiles": "1,2,3", "layers": "3,3,3,3"})])
for n, (x, y) in enumerate([(220, 372), (290, 372), (450, 270), (520, 270), (900, 372)]):
    node(f"Coin{n}", (x, y), level, [("Sprite", {"texture": fnv1a("coin.png"), "width": 20.0, "height": 20.0, "tint": white})])
node("Spikes", (620, 384), level, [("Sprite", {"texture": fnv1a("coin.png"), "width": 28.0, "height": 28.0,
                                               "tint": [1.0, 0.2, 0.2, 1.0]})])
node("Goal", (1150, 360), level, [("Sprite", {"texture": fnv1a("coin.png"), "width": 24.0, "height": 64.0,
                                              "tint": [0.3, 1.0, 0.5, 1.0]})])
node("PickupBurst", (0, 0), level, [("ParticleEmitter", {
    "emitting": False, "rate": 0.0, "burst_count": 0, "lifetime_min": 0.3, "lifetime_max": 0.6, "speed_min": 40.0,
    "speed_max": 120.0, "angle_min": 0.0, "angle_max": 360.0, "gravity": [0.0, 200.0],
    "start_color": [1.0, 0.85, 0.2, 1.0], "end_color": [1.0, 0.4, 0.0, 0.0], "start_size": 5.0, "end_size": 1.0,
    "texture": 0, "atlas_columns": 1, "atlas_rows": 1, "frame": 0, "world_space": True, "max_particles": 64, "seed": 3})])
json.dump({"format_version": 1, "nodes": nodes, "next_id": nid[0] + 1}, open("scenes/main.scene.json", "w"), indent=2)

def action(n, codes):
    return {"name": n, "bindings": [{"device": "keyboard", "code": c} for c in codes]}
project = {"format_version": 1, "name": "platformer_slice", "main_scene": "scenes/main.scene.json",
           "module": "platformer_slice_game.so",
           "input_actions": {"format_version": 1, "actions": [
               action("move_left", [65, 263]), action("move_right", [68, 262]), action("jump", [32])]},
           "hud": {"format_version": 1, "elements": [
               {"type": "text", "text": "SCORE 0", "anchor": "top_left", "offset": [16, 16], "font_scale": 3.0,
                "color": [1, 1, 1, 1]},
               {"type": "progress_bar", "anchor": "top_right", "offset": [-16, 16], "size": [220, 20], "fraction": 1.0,
                "fill": [0.85, 0.2, 0.2, 1], "background": [0, 0, 0, 0.6]},
               {"type": "text", "text": "", "anchor": "center", "offset": [0, -60], "font_scale": 5.0,
                "color": [1, 0.9, 0.3, 1]},
               {"type": "button", "id": "restart", "label": "RESTART", "anchor": "bottom_right",
                "offset": [-16, -16], "size": [160, 40]}]}}
json.dump(project, open("project.json", "w"), indent=2)
