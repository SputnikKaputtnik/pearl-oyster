#!/usr/bin/env python3
"""Moxie .mxm (ModelResource) structure parser — derived from the decompiled
MOXIE::ModelResource::read / readMesh / Material::read / MaterialPass::read (see
docs/file-formats.md). Compressed vertex/index streams are kept as opaque blobs for now.

Usage:
    mxm.py <file.mxm>              print a structural summary
    mxm.py --check <dir>           parse every .mxm below dir, report files that do not end
                                   exactly at EOF or fail
"""
import os
import struct
import sys

MAGIC = 0x0D00D135
TYPE_MODEL = 0xB6181A32


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def take(self, n):
        if self.p + n > len(self.d):
            raise EOFError(f"read {n} at {self.p:#x} beyond {len(self.d):#x}")
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def u8(self): return self.take(1)[0]
    def u16(self): return struct.unpack("<H", self.take(2))[0]
    def s16(self): return struct.unpack("<h", self.take(2))[0]
    def u32(self): return struct.unpack("<I", self.take(4))[0]
    def f32(self): return struct.unpack("<f", self.take(4))[0]

    def string(self):  # MOXIE::String::read
        n = self.u32()
        return self.take(n).decode("latin-1")

    def uri(self):  # MOXIE::Uri::read
        kind = self.u8()
        h = self.u32()
        size = self.u32()
        a = self.u32()
        b = self.u32()
        raw = self.take(size)
        o1, o2, o3 = self.u32(), self.u32(), self.u32()
        parts = raw.split(b"\0")
        full = parts[2].decode("latin-1") if len(parts) > 2 else raw.decode("latin-1")
        return {"kind": kind, "hash": h, "uri": full, "fields": (a, b), "offsets": (o1, o2, o3)}


# Engine enum index -> OpenGL enum (table @0x1802322e0, used by RenderDeviceGL::setRenderStateCached)
GL_ENUM = ("ZERO", "ONE", "SRC_COLOR", "SRC_ALPHA", "DST_COLOR", "DST_ALPHA", "ONE_MINUS_SRC_COLOR",
           "ONE_MINUS_SRC_ALPHA", "ONE_MINUS_DST_COLOR", "ONE_MINUS_DST_ALPHA", "SRC_ALPHA_SATURATE",
           "CONSTANT_COLOR", "CONSTANT_ALPHA", "FUNC_ADD", "FUNC_SUBTRACT", "FUNC_REVERSE_SUBTRACT",
           "MIN", "MAX", "NEVER", "ALWAYS", "LESS", "LEQUAL", "GREATER", "GEQUAL", "NOTEQUAL", "KEEP",
           "REPLACE", "INVERT", "INCR", "DECR", "FRONT", "BACK", "FRONT_AND_BACK", "CW", "CCW",
           "POINT", "LINE", "FILL")


def decode_render_state(b):
    """MOXIE::RenderState (0x94 bytes) as applied by RenderDeviceGL::setRenderStateCached."""
    u = lambda o: struct.unpack_from("<I", b, o)[0]  # noqa: E731
    f = lambda o: struct.unpack_from("<f", b, o)[0]  # noqa: E731
    e = lambda o: GL_ENUM[u(o)] if u(o) < len(GL_ENUM) else u(o)  # noqa: E731
    return {
        "blend": bool(b[0]), "blendSrcRGB": e(4), "blendSrcAlpha": e(8), "blendDstRGB": e(0xC),
        "blendDstAlpha": e(0x10), "blendColor": struct.unpack_from("<4f", b, 0x14),
        "blendEqRGB": e(0x24), "blendEqAlpha": e(0x28), "depthTest": bool(b[0x2C]),
        "depthWrite": bool(b[0x2D]), "depthFunc": e(0x30), "depthRange": (f(0x34), f(0x38)),
        "scissor": bool(b[0x3C]), "scissorRect": struct.unpack_from("<4i", b, 0x40),
        "stencil": bool(b[0x50]), "stencilMask": u(0x54), "stencilFunc": e(0x58), "stencilRef": u(0x5C),
        "stencilReadMask": u(0x60), "stencilOps": (e(0x64), e(0x68), e(0x6C)),
        "colorMask": tuple(bool(x) for x in b[0x70:0x74]), "cull": bool(b[0x74]), "cullFace": e(0x78),
        "polygonMode": e(0x7C), "polygonOffset": bool(b[0x80]), "polygonOffsetFU": (f(0x84), f(0x88)),
        "dither": bool(b[0x8C]), "f32_90": f(0x90),
    }


def read_param(r):  # MaterialParameter base + subclass payload
    ptype = r.u16()
    name_hash = r.u32()
    base = {"type": ptype, "hash": name_hash}
    base["u16_e"] = r.u16()
    size = r.u32()
    base["size"] = size
    base["u32_14"] = r.u32()
    base["u8_1c"] = r.u8()
    if ptype == 0:  # constant (MaterialParameterConstant)
        base["values"] = struct.unpack(f"<{size // 4}f", r.take(size // 4 * 4))
    else:  # texture (MaterialParameterTexture)
        base["uri"] = r.uri()["uri"]
        base["tex_kind"] = r.u16()
        n = r.u32()
        base["samplers"] = [struct.unpack("<6If", r.take(28)) for _ in range(n)]
    return base


def read_material(r):
    ver = r.u32()
    if ver != 2:
        raise ValueError(f"material version {ver}")
    name = r.string()
    passes = []
    for _ in range(r.u32()):
        shader = r.uri()["uri"]
        state = r.take(0x94)
        u_d4 = r.u32()
        params = [read_param(r) for _ in range(r.u32())]
        passes.append({"shader": shader, "state": state, "render_state": decode_render_state(state),
                       "u_d4": u_d4, "params": params})
    return {"name": name, "passes": passes}


def read_mesh(r, version):
    m = {"u32_11c": r.u32(), "u32_120": r.u32()}
    n = r.u16()
    m["u16_list"] = struct.unpack(f"<{n}H", r.take(2 * n))
    ninst = 0
    if version > 0x20029:
        ninst = r.u32()
        m["instances"] = [r.take(0x28) for _ in range(ninst)]
    if version < 0x2002B:
        raise NotImplementedError("uncompressed vertex streams (version < 0x2002B)")
    size = r.u32()
    m["vertex_blob"] = (r.p, size)
    r.take(size)
    m["u16_118"] = r.u16()
    n = r.u16()
    m["matrices"] = [struct.unpack("<16f", r.take(64)) for _ in range(n)]
    if version < 0x2002C:
        raise NotImplementedError("uncompressed indices (version < 0x2002C)")
    size = r.u32()
    m["index_blob"] = (r.p, size)
    r.take(size)
    has_surface = r.u32()
    if has_surface:
        m["surface_type"] = r.u32()
        if m["surface_type"] != 0:
            raise NotImplementedError(f"geometry surface type {m['surface_type']}")
        m["subdiv"] = read_subdiv(r)
    n = r.u16()
    m["tail12"] = [struct.unpack("<3I", r.take(12)) for _ in range(n)]
    return m


SUBDIV_COMPONENTS = (3, 3, 4, 2, 2, 2, 2, 4)  # table @0x1802325f0: pos, nrm, tan, uv0..3, color


def read_subdiv(r):
    """MOXIE::SubdivSurface::read (vtable slot +8) -> FUN_1801b7340."""
    s = {}
    nv, flags = r.u32(), r.u32()
    s["num_vertices"], s["attr_flags"] = nv, flags
    s["attributes"] = {}
    for k, comps in enumerate(SUBDIV_COMPONENTS):
        if flags & (1 << k):
            s["attributes"][k] = (r.p, comps)
            r.take(4 * comps * nv)
    if r.u8():  # skinning: 4 weights (float) + 4 joint indices (bytes) per vertex
        s["skin"] = (r.p, nv)
        r.take(16 * nv + 4 * nv)
    # topology header: 8 x u32 (Ghidra dropped the element count of this read):
    # [0] = length of the per-face byte array, [1] = ?, [2..7] = face-varying value counts
    # for attributes 2..7 (tangent, uv0..3, color)
    hdr = struct.unpack("<8I", r.take(32))
    s["topo_header"] = hdr
    s["face_bytes"] = r.take(hdr[0])
    nidx = r.u32()
    s["num_indices"] = nidx
    s["indices_at"] = r.p
    r.take(4 * nidx)
    s["fvar_values"] = {}
    for k in range(2, 8):
        if hdr[k]:
            s["fvar_values"][k] = (r.p, hdr[k], SUBDIV_COMPONENTS[k])
            r.take(4 * hdr[k] * SUBDIV_COMPONENTS[k])
    s["fvar_indices_at"] = r.p
    r.take(4 * nidx)
    a, b, c = r.u32(), r.u32(), r.u32()
    s["block_d"] = (a, b, c)
    r.take(4 * b)
    n0, n1 = r.u32(), r.u32()
    s["block_e"] = (n0, n1, r.take(8))  # 8-byte field at +0x8 (element count dropped by Ghidra)
    for _ in range(n0):
        r.take(4 * n1)
    return s


def parse(data):
    r = Reader(data)
    magic, tid, version = r.u32(), r.u32(), r.u32()
    if magic != MAGIC or tid != TYPE_MODEL:
        raise ValueError("not a Moxie model")
    if not 0x20029 <= version <= 0x2002E:
        raise ValueError(f"version {version:#x} out of range")
    model = {"version": version, "flags": 0}
    if version > 0x2002C:
        model["flags"] = r.u32()
        if model["flags"] & 0x1FF:
            # patch model: reuses the geometry of a base model, optionally overriding materials
            model["base_model"] = r.string()
            if model["flags"] & 1:
                model["materials"] = [read_material(r) for _ in range(r.u16())]
            model["end"] = r.p
            return model
    model["materials"] = [read_material(r) for _ in range(r.u16())]
    n = r.u16()
    model["local_xforms"] = [struct.unpack("<16f", r.take(64)) for _ in range(n)]
    if n:
        model["xform_names"] = [r.string() for _ in range(n)]
        model["xform_props"] = [r.string() for _ in range(n)]
    n = r.u16()
    model["bind_xforms"] = [struct.unpack("<16f", r.take(64)) for _ in range(n)]
    n = r.u16()
    model["parents"] = struct.unpack(f"<{n}h", r.take(2 * n))
    model["meshes"] = [read_mesh(r, version) for _ in range(r.u16())]
    model["bone_groups"] = []
    for _ in range(r.u16()):
        k = r.u16()
        model["bone_groups"].append(struct.unpack(f"<{k}H", r.take(2 * k)))
    model["end"] = r.p
    return model


def summary(path):
    data = open(path, "rb").read()
    m = parse(data)
    print(f"{path}: version {m['version']:#x}, {len(data)} bytes, parsed to {m['end']:#x}")
    for mat in m["materials"]:
        print(f"  material {mat['name']}: " + ", ".join(p["shader"].rsplit("/", 1)[-1] for p in mat["passes"]))
        for p in mat["passes"]:
            for prm in p["params"]:
                if "uri" in prm:
                    print(f"      tex {prm['uri']} kind={prm['tex_kind']} samplers={prm['samplers']}")
    print(f"  transforms: {len(m['local_xforms'])} local, {len(m['bind_xforms'])} bind")
    if m.get("xform_names"):
        print("   ", m["xform_names"][:8])
    for i, me in enumerate(m["meshes"]):
        print(f"  mesh {i}: list={me['u16_list'][:6]} inst={len(me.get('instances', []))} "
              f"vblob={me['vertex_blob'][1]} iblob={me['index_blob'][1]} mat={me['u16_118']} "
              f"mats={len(me['matrices'])} tail={me['tail12'][:3]}")


def check(root):
    ok, bad = 0, []
    for dp, _, fns in os.walk(root):
        for fn in fns:
            if not fn.lower().endswith(".mxm"):
                continue
            p = os.path.join(dp, fn)
            data = open(p, "rb").read()
            try:
                m = parse(data)
                if m["end"] == len(data):
                    ok += 1
                else:
                    bad.append((p, f"ends at {m['end']:#x} of {len(data):#x}"))
            except Exception as e:  # noqa: BLE001
                bad.append((p, f"{type(e).__name__}: {e}"))
    print(f"{ok} files parsed exactly to EOF, {len(bad)} not")
    kinds = {}
    for p, msg in bad:
        kinds.setdefault(msg.split(":")[0].split(" ")[0], []).append((p, msg))
    for k, lst in kinds.items():
        print(f"  {k}: {len(lst)}  e.g. {os.path.basename(lst[0][0])}: {lst[0][1]}")
    return 0 if not bad else 1


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        sys.exit(check(sys.argv[2]))
    summary(sys.argv[1])
