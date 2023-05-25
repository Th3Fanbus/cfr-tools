/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfr.h"

#define ALIGN(x, a)		__ALIGN_MASK(x, (__typeof__(x))(a)-1UL)
#define __ALIGN_MASK(x, mask)	(((x)+(mask))&~(mask))
#define ALIGN_UP(x, a)		ALIGN((x), (a))
#define ALIGN_DOWN(x, a)	((x) & ~((__typeof__(x))(a)-1UL))
#define IS_ALIGNED(x, a)	(((x) & ((__typeof__(x))(a)-1UL)) == 0)

static char *lb_new_record(struct lb_header *header)
{
	assert(header);
	return header->buffer;
}

static uint32_t crc32_byte(uint32_t prev_crc, uint8_t data)
{
	prev_crc ^= (uint32_t)data << 24;

	for (int i = 0; i < 8; i++) {
		if ((prev_crc & 0x80000000UL) != 0)
			prev_crc = ((prev_crc << 1) ^ 0x04C11DB7UL);
		else
			prev_crc <<= 1;
	}

	return prev_crc;
}

static uint32_t crc32(const uint8_t *buf, size_t size)
{
	uint32_t result = 0;
	while (size--) {
		result = crc32_byte(result, *buf++);
	}
	return result;
}

#define CRC(buf, size, crc_func) crc32((const uint8_t *)(buf), (size))

static uint32_t cfr_record_size(const char *startp, const char *endp)
{
	const uintptr_t start = (uintptr_t)startp;
	const uintptr_t end = (uintptr_t)endp;

	if (start > end || end - start > UINT32_MAX) {
		/*
		 * Should never be reached unless something went really
		 * wrong. Record size can never be negative, and things
		 * would break long before record length exceeds 4 GiB.
		 */
		fprintf(stderr, "%s: bad record size (start: %"PRIxPTR", end: %"PRIxPTR")\n",
			__func__, start, end);
		exit(-1);
	}
	return (uint32_t)(end - start);
}

static uint32_t write_cfr_varchar(char *current, const char *string, uint32_t tag)
{
	assert(string);

	struct lb_cfr_varbinary *cfr_str = (struct lb_cfr_varbinary *)current;
	cfr_str->tag = tag;
	cfr_str->data_length = strlen(string) + 1;
	memcpy(cfr_str->data, string, cfr_str->data_length);
	cfr_str->size = ALIGN_UP(sizeof(*cfr_str) + cfr_str->data_length, LB_ENTRY_ALIGN);
	return cfr_str->size;
}

static uint32_t sm_write_string_default_value(char *current, const char *string)
{
	return write_cfr_varchar(current, string, LB_TAG_CFR_VARCHAR_DEF_VALUE);
}

static uint32_t sm_write_opt_name(char *current, const char *string)
{
	return write_cfr_varchar(current, string, LB_TAG_CFR_VARCHAR_OPT_NAME);
}

static uint32_t sm_write_ui_name(char *current, const char *string)
{
	return write_cfr_varchar(current, string, LB_TAG_CFR_VARCHAR_UI_NAME);
}

static uint32_t sm_write_ui_helptext(char *current, const char *string)
{
	if (!string || !strlen(string))
		return 0;

	return write_cfr_varchar(current, string, LB_TAG_CFR_VARCHAR_UI_HELPTEXT);
}

static uint32_t sm_write_enum_value(char *current, const struct sm_enum_value *e)
{
	struct lb_cfr_enum_value *enum_val = (struct lb_cfr_enum_value *)current;
	enum_val->tag = LB_TAG_CFR_ENUM_VALUE;
	enum_val->value = e->value;
	enum_val->size = sizeof(*enum_val);

	current += enum_val->size;
	current += sm_write_ui_name(current, e->ui_name);

	enum_val->size = cfr_record_size((char *)enum_val, current);
	return enum_val->size;
}

static uint32_t write_numeric_option(char *current, uint32_t tag, uint32_t object_id,
		const char *opt_name, const char *ui_name, const char *ui_helptext,
		uint32_t flags, uint32_t default_value, const struct sm_enum_value *values)
{
	struct lb_cfr_numeric_option *option = (struct lb_cfr_numeric_option *)current;
	option->tag = tag;
	option->object_id = object_id;
	option->flags = flags;
	option->default_value = default_value;
	option->size = sizeof(*option);

	current += option->size;
	current += sm_write_opt_name(current, opt_name);
	current += sm_write_ui_name(current, ui_name);
	current += sm_write_ui_helptext(current, ui_helptext);

	if (option->tag == LB_TAG_CFR_OPTION_ENUM && values) {
		for (const struct sm_enum_value *e = values; e->ui_name; e++) {
			current += sm_write_enum_value(current, e);
		}
	}

	option->size = cfr_record_size((char *)option, current);
	return option->size;
}

static uint32_t sm_write_opt_enum(char *current, const struct sm_obj_enum *sm_enum)
{
	return write_numeric_option(current, LB_TAG_CFR_OPTION_ENUM, sm_enum->object_id,
			sm_enum->opt_name, sm_enum->ui_name, sm_enum->ui_helptext,
			sm_enum->flags, sm_enum->default_value, sm_enum->values);
}

static uint32_t sm_write_opt_number(char *current, const struct sm_obj_number *sm_number)
{
	return write_numeric_option(current, LB_TAG_CFR_OPTION_NUMBER, sm_number->object_id,
			sm_number->opt_name, sm_number->ui_name, sm_number->ui_helptext,
			sm_number->flags, sm_number->default_value, NULL);
}

static uint32_t sm_write_opt_bool(char *current, const struct sm_obj_bool *sm_bool)
{
	return write_numeric_option(current, LB_TAG_CFR_OPTION_BOOL, sm_bool->object_id,
			sm_bool->opt_name, sm_bool->ui_name, sm_bool->ui_helptext,
			sm_bool->flags, sm_bool->default_value, NULL);
}

static uint32_t sm_write_opt_varchar(char *current, const struct sm_obj_varchar *sm_varchar)
{
	struct lb_cfr_varchar_option *option = (struct lb_cfr_varchar_option *)current;
	option->tag = LB_TAG_CFR_OPTION_VARCHAR;
	option->object_id = sm_varchar->object_id;
	option->flags = sm_varchar->flags;
	option->size = sizeof(*option);

	current += option->size;
	current += sm_write_string_default_value(current, sm_varchar->default_value);
	current += sm_write_opt_name(current, sm_varchar->opt_name);
	current += sm_write_ui_name(current, sm_varchar->ui_name);
	current += sm_write_ui_helptext(current, sm_varchar->ui_helptext);

	option->size = cfr_record_size((char *)option, current);
	return option->size;
}

static uint32_t sm_write_opt_comment(char *current, const struct sm_obj_comment *sm_comment)
{
	struct lb_cfr_option_comment *comment = (struct lb_cfr_option_comment *)current;
	comment->tag = LB_TAG_CFR_OPTION_COMMENT;
	comment->object_id = sm_comment->object_id;
	comment->flags = sm_comment->flags;
	comment->size = sizeof(*comment);

	current += comment->size;
	current += sm_write_ui_name(current, sm_comment->ui_name);
	current += sm_write_ui_helptext(current, sm_comment->ui_helptext);

	comment->size = cfr_record_size((char *)comment, current);
	return comment->size;
}

static uint32_t sm_write_object(char *current, const struct sm_object *sm_obj);

static uint32_t sm_write_form(char *current, const struct sm_obj_form *sm_form)
{
	struct lb_cfr_option_form *form = (struct lb_cfr_option_form *)current;
	form->tag = LB_TAG_CFR_OPTION_FORM;
	form->object_id = sm_form->object_id;
	form->flags = sm_form->flags;
	form->size = sizeof(*form);

	current += form->size;
	current += sm_write_ui_name(current, sm_form->ui_name);
	for (size_t i = 0; i < sm_form->num_objects; i++) {
		current += sm_write_object(current, &sm_form->obj_list[i]);
	}

	form->size = cfr_record_size((char *)form, current);
	return form->size;
}

static uint32_t sm_write_object(char *current, const struct sm_object *sm_obj)
{
	assert(sm_obj);

	switch (sm_obj->kind) {
	case SM_OBJ_NONE:
		return 0;
	case SM_OBJ_ENUM:
		return sm_write_opt_enum(current, &sm_obj->sm_enum);
	case SM_OBJ_NUMBER:
		return sm_write_opt_number(current, &sm_obj->sm_number);
	case SM_OBJ_BOOL:
		return sm_write_opt_bool(current, &sm_obj->sm_bool);
	case SM_OBJ_VARCHAR:
		return sm_write_opt_varchar(current, &sm_obj->sm_varchar);
	case SM_OBJ_COMMENT:
		return sm_write_opt_comment(current, &sm_obj->sm_comment);
	case SM_OBJ_FORM:
		return sm_write_form(current, &sm_obj->sm_form);
	default:
		fprintf(stderr, "Unknown setup menu object kind %u, ignoring\n", sm_obj->kind);
		return 0;
	}
}

void cfr_write_setup_menu(struct lb_header *header, const struct setup_menu_root *sm_root)
{
	assert(sm_root);

	char *current = (char *)lb_new_record(header);
	struct lb_cfr *menu = (struct lb_cfr *)current;
	menu->tag = LB_TAG_CFR;
	menu->size = sizeof(*menu);

	current += menu->size;
	for (size_t i = 0; i < sm_root->num_forms; i++) {
		current += sm_write_form(current, &sm_root->form_list[i]);
	}

	menu->size = cfr_record_size((char *)menu, current);

	menu->checksum = 0;
	menu->checksum = CRC(menu, menu->size, crc32_byte);

	printf("CFR: Written %u bytes of CFR structures at %p, with CRC32 0x%08x\n",
		menu->size, (char *)menu, menu->checksum);
}
