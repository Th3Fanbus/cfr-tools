/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfr.h"

/* TODO: This may need to be global, or removed if auto-generating the data */
static uint32_t atlas_get_object_id(void)
{
	static uint32_t object_id = 0;

	/* Let's start at 1: this way, option ID being 0 indicates someone messed up */
	return ++object_id;
}

/*
 * TODO: Writing this by hand is extremely tedious. Introducing a DSL
 * (Domain-Specific Language) to describe options which is translated
 * into code at build time may be the way to go. Maybe expand SCONFIG
 * so that these can be devicetree options?
 */
static void lb_board(struct lb_header *header)
{
	const bool rt_perf = false;
	const bool pf_ok = true;

	const struct sm_obj_varchar serial_number = {
		.object_id	= atlas_get_object_id(),
		.flags		= CFR_OPTFLAG_READONLY | CFR_OPTFLAG_VOLATILE,
		.opt_name	= "serial_number",
		.ui_name	= "Serial Number",
		.default_value	= "serialnumber",
	};

	const struct sm_obj_varchar part_number = {
		.object_id	= atlas_get_object_id(),
		.flags		= CFR_OPTFLAG_READONLY | CFR_OPTFLAG_VOLATILE,
		.opt_name	= "part_number",
		.ui_name	= "Part Number",
		.default_value	= "partnumber",
	};

	const struct sm_obj_comment bad_profile = {
		.object_id	= atlas_get_object_id(),
		.flags		= CFR_OPTFLAG_READONLY | (pf_ok ? CFR_OPTFLAG_SUPPRESS : 0),
		.ui_name	= "WARNING: Profile code is invalid",
	};

	const struct sm_obj_number profile = {
		.object_id	= atlas_get_object_id(),
		.flags		= CFR_OPTFLAG_READONLY | CFR_OPTFLAG_VOLATILE,
		.opt_name	= "profile",
		.ui_name	= "Profile code",
		.ui_helptext	= "The profile code obtained from the EEPROM",
		.default_value	= 42,
	};

	const struct sm_enum_value pwr_after_g3_values[] = {
		{ "Power off (S5)", 0 },
		{ "Power on (S0)",  1 },
		/* No support for previous/last power state */
		SM_ENUM_VALUE_END,
	};
	const struct sm_obj_enum power_on_after_fail = {
		.object_id	= atlas_get_object_id(),
		.opt_name	= "power_on_after_fail",
		.ui_name	= "Restore AC Power Loss",
		.ui_helptext	= "Specify what to do when power is re-applied "
				  "after a power loss. This option has no effect "
				  "on systems without a RTC battery.",
		.default_value	= 0,
		.values		= pwr_after_g3_values,
	};

	const struct sm_enum_value primary_display_values[] = {
		{ "Intel iGPU",    0 },
		{ "CPU PEG dGPU",  1 },
		{ "PCH PCIe dGPU", 2 },
		{ "Auto",          3 },
		SM_ENUM_VALUE_END,
	};
	const struct sm_obj_enum primary_display = {
		.object_id	= atlas_get_object_id(),
		.opt_name	= "primary_display",
		.ui_name	= "Primary display device",
		.ui_helptext	= "Specify which display device to use as primary.",
		.default_value	= 3,
		.values		= primary_display_values,
	};

	const struct sm_enum_value pkg_c_state_limit_values[] = {
		{ "C0/C1",     0 },
		{ "C2",        1 },
		{ "C3",        2 },
		{ "C6",        3 },
		{ "C7",        4 },
		{ "C7S",       5 },
		{ "C8",        6 },
		{ "C9",        7 },
		{ "C10",       8 },
		{ "Default", 254 },
		{ "Auto",    255 },
		SM_ENUM_VALUE_END,
	};
	const struct sm_obj_enum pkg_c_state_limit = {
		.object_id	= atlas_get_object_id(),
		.flags		= rt_perf ? CFR_OPTFLAG_SUPPRESS : 0,
		.opt_name	= "pkg_c_state_limit",
		.ui_name	= "Package C-state limit",
		.ui_helptext	= "", /* TODO: write something */
		.default_value	= rt_perf ? 0 : 255,
		.values		= pkg_c_state_limit_values,
	};

#define NUM_PCIE_SSC_SETTINGS	20
	struct sm_enum_value pch_pm_pcie_pll_ssc_values[] = {
		[NUM_PCIE_SSC_SETTINGS] = { "Auto", 0xff },
		SM_ENUM_VALUE_END,
	};
	for (unsigned int i = 0; i < NUM_PCIE_SSC_SETTINGS; i++) {
		char buffer[16];
		snprintf(buffer, sizeof(buffer), "%u.%u%%", i / 10, i % 10);
		pch_pm_pcie_pll_ssc_values[i].ui_name = strdup(buffer);
		pch_pm_pcie_pll_ssc_values[i].value = i;
	}
	const struct sm_obj_enum pch_pcie_pll_ssc = {
		.object_id	= atlas_get_object_id(),
		.opt_name	= "pch_pcie_pll_ssc",
		.ui_name	= "PCH PCIe PLL Spread Spectrum Clocking",
		/* No help text */
		.default_value	= 0xff,
		.values		= pch_pm_pcie_pll_ssc_values,
	};

	const struct sm_obj_bool c_states = {
		.object_id	= atlas_get_object_id(),
		.flags		= rt_perf ? CFR_OPTFLAG_SUPPRESS : 0,
		.opt_name	= "c_states",
		.ui_name	= "CPU power states (C-states)",
		.ui_helptext	= "Specify whether C-states are supported.",
		.default_value	= rt_perf ? false : true,
	};

	const struct sm_obj_bool hyper_threading = {
		.object_id	= atlas_get_object_id(),
		.flags		= rt_perf ? CFR_OPTFLAG_SUPPRESS : 0,
		.opt_name	= "hyper_threading",
		.ui_name	= "Hyper-Threading Technology",
		/* No help text */
		.default_value	= rt_perf ? false : true,
	};

	const struct sm_obj_bool turbo_mode = {
		.object_id	= atlas_get_object_id(),
		.opt_name	= "turbo_mode",
		.ui_name	= "Turbo Boost",
		/* No help text */
		.default_value	= true,
	};

	const struct sm_obj_bool energy_eff_turbo = {
		.object_id	= atlas_get_object_id(),
		.flags		= rt_perf ? CFR_OPTFLAG_SUPPRESS : 0,
		.opt_name	= "energy_eff_turbo",
		.ui_name	= "Energy Efficient Turbo",
		/* No help text */
		.default_value	= false,
	};

	const struct sm_obj_bool vmx = {
		.object_id	= atlas_get_object_id(),
		.opt_name	= "vmx",
		.ui_name	= "Intel Virtualization Technology (VT-x)",
		/* No help text */
		.default_value	= false,
	};

	const struct sm_obj_bool vtd = {
		.object_id	= atlas_get_object_id(),
		.opt_name	= "vtd",
		.ui_name	= "Intel Virtualization Technology for Directed I/O (VT-d)",
		/* No help text */
		.default_value	= false,
	};

	const struct sm_obj_bool ibecc = {
		.object_id	= atlas_get_object_id(),
		.opt_name	= "ibecc",
		.ui_name	= "In-Band ECC",
		.ui_helptext	= "Specify whether In-Band error checking and "
				  "correction is to be enabled. Enabling this "
				  "option will reduce the amount of available "
				  "RAM because some memory is needed to store "
				  "ECC codes.",
		.default_value	= false,
	};

	const struct sm_obj_bool llc_dead_line = {
		.object_id	= atlas_get_object_id(),
		.opt_name	= "llc_dead_line",
		.ui_name	= "LLC Dead Line Allocation",
		.ui_helptext	= "", /* TODO: figure out */
		.default_value	= false,
	};

	const struct sm_obj_bool pcie_sris = {
		.object_id	= atlas_get_object_id(),
		.opt_name	= "pcie_sris",
		.ui_name	= "PCIe Separate Reference Clock with Independent SSC",
		.ui_helptext	= "", /* TODO: figure out */
		.default_value	= false,
	};

/* TODO: Move to cfr.h or refactor to avoid this */
#define SM_DECLARE_ENUM(structure)	{ .kind = SM_OBJ_ENUM,    .sm_enum    = structure }
#define SM_DECLARE_NUMBER(structure)	{ .kind = SM_OBJ_NUMBER,  .sm_number  = structure }
#define SM_DECLARE_BOOL(structure)	{ .kind = SM_OBJ_BOOL,    .sm_bool    = structure }
#define SM_DECLARE_VARCHAR(structure)	{ .kind = SM_OBJ_VARCHAR, .sm_varchar = structure }
#define SM_DECLARE_COMMENT(structure)	{ .kind = SM_OBJ_COMMENT, .sm_comment = structure }
#define SM_DECLARE_FORM(structure)	{ .kind = SM_OBJ_FORM,    .sm_form    = structure }

	const struct sm_object main_contents[] = {
		SM_DECLARE_VARCHAR(serial_number),
		SM_DECLARE_VARCHAR(part_number),
		SM_DECLARE_COMMENT(bad_profile),
		SM_DECLARE_NUMBER(profile),
		SM_DECLARE_ENUM(power_on_after_fail),
		SM_DECLARE_ENUM(primary_display),
		SM_DECLARE_ENUM(pkg_c_state_limit),
		SM_DECLARE_ENUM(pch_pcie_pll_ssc),
		SM_DECLARE_BOOL(c_states),
		SM_DECLARE_BOOL(hyper_threading),
		SM_DECLARE_BOOL(turbo_mode),
		SM_DECLARE_BOOL(energy_eff_turbo),
		SM_DECLARE_BOOL(vmx),
		SM_DECLARE_BOOL(vtd),
		SM_DECLARE_BOOL(ibecc),
		SM_DECLARE_BOOL(llc_dead_line),
		SM_DECLARE_BOOL(pcie_sris),
	};

	const struct sm_obj_form root_contents[] = {
		{
			.object_id	= atlas_get_object_id(),
			.ui_name	= "Main",
			.obj_list	= main_contents,
			.num_objects	= ARRAY_SIZE(main_contents),
		},
	};

	const struct setup_menu_root sm_root = {
		.form_list	= root_contents,
		.num_forms	= ARRAY_SIZE(root_contents),
	};

	cfr_write_setup_menu(header, &sm_root);
}

static int save_to_file(const char *filename, const char *data, size_t length)
{
	printf("Saving to '%s'\n", filename);

	FILE *stream = fopen(filename, "wb");
	if (!stream) {
		perror("Error opening file");
		return -1;
	}

	int ret = 0;
	if (fwrite(data, sizeof(data[0]), length, stream) != length) {
		perror("Problems writing data");
		ret = -1;
	}

	fclose(stream);
	return ret;
}

static int dump_formatted(FILE *stream, const char *data, size_t length)
{
	fprintf(stream, "static __attribute__((aligned(4))) uint8_t cfr_raw_data[] = {");
	for (size_t i = 0; i < length; i++) {
		if ((i % 16) == 0) {
			fprintf(stream, "\n\t");
		}
		fprintf(stream, "0x%02x, ", ((const uint8_t *)data)[i]);
	}
	fprintf(stream, "\n};\n");
	return 0;
}

static size_t cfr_size(const char *buffer)
{
	const struct lb_record *rec = (const struct lb_record *)buffer;
	assert(rec->tag == LB_TAG_CFR);
	return rec->size;
}

int main(int argc, char **argv)
{
	static __attribute__((aligned(4))) char buffer[32 * 1024] = {0};

	if (argc > 2) {
		fprintf(stderr, "Usage: cfr_write [output file]\n");
		return -1;
	}

	struct lb_header header = { .buffer = buffer };
	lb_board(&header);

	if (argc == 2) {
		return save_to_file(argv[1], buffer, cfr_size(buffer));
	} else {
		return dump_formatted(stdout, buffer, cfr_size(buffer));
	}
}
