/* radare2 - LGPL - Copyright 2017 - pancake */

// TODO: Support NRR and MODF
#include <r_types.h>
#include <r_util.h>
#include <r_lib.h>
#include <r_bin.h>
#include "nxo/nxo.h"

#define NRO_OFF(x) (sizeof (NXOStart) + r_offsetof (NROHeader, x))
#define NRO_OFFSET_MODMEMOFF r_offsetof (NXOStart, mod_memoffset)

// starting at 0x10 (16th byte)
typedef struct {
	ut32 magic;  // NRO0
	ut32 unknown; // 4
	ut32 size; // 8
	ut32 unknown2; // 12
	ut32 text_memoffset; // 16
	ut32 text_size; // 20
	ut32 ro_memoffset; // 24
	ut32 ro_size; // 28
	ut32 data_memoffset; // 32
	ut32 data_size; // 36
	ut32 bss_size; // 40
	ut32 unknown3;
} NROHeader;

static ut64 baddr(RBinFile *bf) {
	return bf? readLE32 (bf->buf, NRO_OFFSET_MODMEMOFF): 0;
}

static bool check_bytes(const ut8 *buf, ut64 length) {
	if (buf && length >= 0x20) {
		return fileType (buf + NRO_OFF (magic)) != NULL;
	}
	return false;
}

static void *load_bytes(RBinFile *bf, const ut8 *buf, ut64 sz, ut64 loadaddr, Sdb *sdb) {
	RBinNXOObj *bin = R_NEW0 (RBinNXOObj);
	if (!bin) {
		return NULL;
	}
	ut64 ba = baddr (bf);
	bin->methods_list = r_list_newf ((RListFree)free);
	bin->imports_list = r_list_newf ((RListFree)free);
	bin->classes_list = r_list_newf ((RListFree)free);
	ut32 mod0 = readLE32 (bf->buf, NRO_OFFSET_MODMEMOFF);
	parseMod (bf->buf, bin, mod0, ba);
	return (void *) bin;//(size_t) check_bytes (buf, sz);
}

static bool load(RBinFile *bf) {
	if (!bf || !bf->buf || !bf->o) {
		return false;
	}
	const ut64 sz = r_buf_size (bf->buf);
	const ut64 la = bf->o->loadaddr;
	const ut8 *bytes = r_buf_buffer (bf->buf);
	bf->o->bin_obj = load_bytes (bf, bytes, sz, la, bf->sdb);
	return bf->o->bin_obj != NULL;
}

static int destroy(RBinFile *bf) {
	return true;
}

static RBinAddr *binsym(RBinFile *bf, int type) {
	return NULL; // TODO
}

static RList *entries(RBinFile *bf) {
	RList *ret;
	RBinAddr *ptr = NULL;
	if (!(ret = r_list_new ())) {
		return NULL;
	}
	ret->free = free;
	if ((ptr = R_NEW0 (RBinAddr))) {
		ptr->paddr = 0x80;
		ptr->vaddr = ptr->paddr + baddr (bf);
		r_list_append (ret, ptr);
	}
	return ret;
}

static Sdb *get_sdb(RBinFile *bf) {
	Sdb *kv = sdb_new0 ();
	sdb_num_set (kv, "nro_start.offset", 0, 0);
	sdb_num_set (kv, "nro_start.size", 16, 0);
	sdb_set (kv, "nro_start.format", "xxq unused mod_memoffset padding", 0);
	sdb_num_set (kv, "nro_header.offset", 16, 0);
	sdb_num_set (kv, "nro_header.size", 0x70, 0);
	sdb_set (kv, "nro_header.format", "xxxxxxxxxxxx magic unk size unk2 text_offset text_size ro_offset ro_size data_offset data_size bss_size unk3", 0);
	sdb_ns_set (bf->sdb, "info", kv);
	return kv;
}

static RList *sections(RBinFile *bf) {
	RList *ret = NULL;
	RBinSection *ptr = NULL;
	RBuffer *b = bf->buf;
	if (!bf->o->info) {
		return NULL;
	}
	if (!(ret = r_list_new ())) {
		return NULL;
	}
	ret->free = free;

	ut64 ba = baddr (bf);

	if (!(ptr = R_NEW0 (RBinSection))) {
		return ret;
	}
	strncpy (ptr->name, "header", R_BIN_SIZEOF_STRINGS);
	ptr->size = 0x80;
	ptr->vsize = 0x80;
	ptr->paddr = 0;
	ptr->vaddr = 0;
	ptr->srwx = R_BIN_SCN_READABLE;
	ptr->add = false;
	r_list_append (ret, ptr);

	int bufsz = r_buf_size (bf->buf);

	ut32 mod0 = readLE32 (bf->buf, NRO_OFFSET_MODMEMOFF);
	if (mod0 && mod0 + 8 < bufsz) {
		if (!(ptr = R_NEW0 (RBinSection))) {
			return ret;
		}
		ut32 mod0sz = readLE32 (bf->buf, mod0 + 4);
		strncpy (ptr->name, "mod0", R_BIN_SIZEOF_STRINGS);
		ptr->size = mod0sz;
		ptr->vsize = mod0sz;
		ptr->paddr = mod0;
		ptr->vaddr = mod0 + ba;
		ptr->srwx = R_BIN_SCN_READABLE; // rw-
		ptr->add = false;
		r_list_append (ret, ptr);
	} else {
		eprintf ("Invalid MOD0 address\n");
	}

	ut32 sig0 = readLE32 (bf->buf, 0x18);
	if (sig0 && sig0 + 8 < bufsz) {
		if (!(ptr = R_NEW0 (RBinSection))) {
			return ret;
		}
		ut32 sig0sz = readLE32 (bf->buf, sig0 + 4);
		strncpy (ptr->name, "sig0", R_BIN_SIZEOF_STRINGS);
		ptr->size = sig0sz;
		ptr->vsize = sig0sz;
		ptr->paddr = sig0;
		ptr->vaddr = sig0 + ba;
		ptr->srwx = R_BIN_SCN_READABLE; // r--
		ptr->add = true;
		r_list_append (ret, ptr);
	} else {
		eprintf ("Invalid SIG0 address\n");
	}

	// add text segment
	if (!(ptr = R_NEW0 (RBinSection))) {
		return ret;
	}
	strncpy (ptr->name, "text", R_BIN_SIZEOF_STRINGS);
	ptr->vsize = readLE32 (b, NRO_OFF (text_size));
	ptr->size = ptr->vsize;
	ptr->paddr = readLE32 (b, NRO_OFF (text_memoffset));
	ptr->vaddr = ptr->paddr + ba;
	ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_EXECUTABLE; // r-x
	ptr->add = true;
	r_list_append (ret, ptr);

	// add ro segment
	if (!(ptr = R_NEW0 (RBinSection))) {
		return ret;
	}
	strncpy (ptr->name, "ro", R_BIN_SIZEOF_STRINGS);
	ptr->vsize = readLE32 (b, NRO_OFF (ro_size));
	ptr->size = ptr->vsize;
	ptr->paddr = readLE32 (b, NRO_OFF (ro_memoffset));
	ptr->vaddr = ptr->paddr + ba;
	ptr->srwx = R_BIN_SCN_READABLE; // r-x
	ptr->add = true;
	r_list_append (ret, ptr);

	// add data segment
	if (!(ptr = R_NEW0 (RBinSection))) {
		return ret;
	}
	strncpy (ptr->name, "data", R_BIN_SIZEOF_STRINGS);
	ptr->vsize = readLE32 (b, NRO_OFF (data_size));
	ptr->size = ptr->vsize;
	ptr->paddr = readLE32 (b, NRO_OFF (data_memoffset));
	ptr->vaddr = ptr->paddr + ba;
	ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_WRITABLE; // rw-
	ptr->add = true;
	eprintf ("Base Address 0x%08"PFMT64x "\n", ba);
	eprintf ("BSS Size 0x%08"PFMT64x "\n", (ut64)
			readLE32 (bf->buf, NRO_OFF (bss_size)));
	r_list_append (ret, ptr);
	return ret;
}

static RList *symbols(RBinFile *bf) {
	RBinNXOObj *bin;
	if (!bf || !bf->o || !bf->o->bin_obj) {
		return NULL;
	}
	bin = (RBinNXOObj*) bf->o->bin_obj;
	if (!bin) {
		return NULL;
	}
	return bin->methods_list;
}

static RList *imports(RBinFile *bf) {
	RBinNXOObj *bin;
	if (!bf || !bf->o || !bf->o->bin_obj) {
		return NULL;
	}
	bin = (RBinNXOObj*) bf->o->bin_obj;
	if (!bin) {
		return NULL;
	}
	return bin->imports_list;
}

static RList *libs(RBinFile *bf) {
	return NULL;
}

static RBinInfo *info(RBinFile *bf) {
	RBinInfo *ret = R_NEW0 (RBinInfo);
	if (!ret) {
		return NULL;
	}
	const char *ft = fileType (r_buf_get_at (bf->buf, NRO_OFF (magic), NULL));
	if (!ft) {
		ft = "nro";
	}
	ret->file = strdup (bf->file);
	ret->rclass = strdup (ft);
	ret->os = strdup ("switch");
	ret->arch = strdup ("arm");
	ret->machine = strdup ("Nintendo Switch");
	ret->subsystem = strdup (ft);
	if (!strncmp (ft, "nrr", 3)) {
		ret->bclass = strdup ("program");
		ret->type = strdup ("EXEC (executable file)");
	} else if (!strncmp (ft, "nro", 3)) {
		ret->bclass = strdup ("object");
		ret->type = strdup ("OBJECT (executable code)");
	} else { // mod
		ret->bclass = strdup ("library");
		ret->type = strdup ("MOD (executable library)");
	}
	ret->bits = 64;
	ret->has_va = true;
	ret->has_lit = true;
	ret->big_endian = false;
	ret->dbg_info = 0;
	ret->dbg_info = 0;
	return ret;
}

#if !R_BIN_NRO

RBinPlugin r_bin_plugin_nro = {
	.name = "nro",
	.desc = "Nintendo Switch NRO0 binaries",
	.license = "MIT",
	.load = &load,
	.load_bytes = &load_bytes,
	.destroy = &destroy,
	.check_bytes = &check_bytes,
	.baddr = &baddr,
	.binsym = &binsym,
	.entries = &entries,
	.sections = &sections,
	.get_sdb = &get_sdb,
	.symbols = &symbols,
	.imports = &imports,
	.info = &info,
	.libs = &libs,
};

#ifndef CORELIB
R_API RLibStruct radare_plugin = {
	.type = R_LIB_TYPE_BIN,
	.data = &r_bin_plugin_nro,
	.version = R2_VERSION
};
#endif
#endif
