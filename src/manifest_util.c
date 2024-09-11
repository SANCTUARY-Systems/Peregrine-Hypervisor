/*
 * Copyright (c) 2023 SANCTUARY Systems GmbH
 *
 * This file is free software: you may copy, redistribute and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * For a commercial license, please contact SANCTUARY Systems GmbH
 * directly at info@sanctuary.dev
 */

#ifndef EXTERNAL_INCLUDE
#include "pg/manifest.h"
#include "pg/manifest_util.h"

#include "pg/addr.h"
#include "pg/check.h"
#include "pg/dlog.h"
#include "pg/fdt.h"
#include "pg/static_assert.h"
#include "pg/std.h"
#include "pg/string.h"

#endif


#define TRY(expr)                                            \
	do {                                                 \
		enum manifest_return_code ret_code = (expr); \
		if (ret_code != MANIFEST_SUCCESS) {          \
			return ret_code;                     \
		}                                            \
	} while (0)

/**
 * Read a boolean property: true if present; false if not. If present, the value
 * of the property must be empty else it is considered malformed.
 */
enum manifest_return_code read_bool(uint16_t vm_id,const struct fdt_node *node,
					   const char *property, bool *out)
{
	struct memiter data;
	bool present = fdt_read_property(node, property, &data);

	if (!present) {
		dlog_debug("[VM %u] Not found: %s\n", vm_id, property);
	}

	if (present && memiter_size(&data) != 0) {
		return MANIFEST_ERROR_MALFORMED_BOOLEAN;
	}

	*out = present;
	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_optional_char_arr(uint16_t vm_id, const struct fdt_node *node,
		const char *property, struct string_bundle *out) {
	struct memiter data;

	if (!fdt_read_property(node, property, &data)) {
		dlog_debug("[VM %u] Not found: %s\n", vm_id, property);
		out->base = NULL;
		out->size = 0;
		return MANIFEST_SUCCESS;
	}

	out->base = (char *) memiter_base(&data);
	out->size = memiter_size(&data);

	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_optional_char_arr_sh(uint16_t vm_id, const struct fdt_node *node,
		const char *property, struct string_bundle_sh *out) {
	struct memiter data;

	if (!fdt_read_property(node, property, &data)) {
		dlog_debug("[VM %u] Not found: %s\n", vm_id, property);
		out->base = NULL;
		BASE_PTR_TMP(out) = NULL;
		out->size = 0;
		return MANIFEST_SUCCESS;
	}

	out->base = NULL;
	BASE_PTR_TMP(out) = (char *) memiter_base(&data);
	out->size = memiter_size(&data);

	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_string(uint16_t vm_id, const struct fdt_node *node,
					     const char *property, struct string *out)
{
	struct memiter data;

	if (!fdt_read_property(node, property, &data)) {
		dlog_debug("[VM %u] Not found: %s\n", vm_id, property);
		return MANIFEST_ERROR_PROPERTY_NOT_FOUND;
	}

	switch (string_init(out, &data)) {
	case STRING_SUCCESS:
		return MANIFEST_SUCCESS;
	case STRING_ERROR_INVALID_INPUT:
		return MANIFEST_ERROR_MALFORMED_STRING;
	case STRING_ERROR_TOO_LONG:
		return MANIFEST_ERROR_STRING_TOO_LONG;
	default:
		return MANIFEST_ERROR_MALFORMED_STRING;
	}
}

enum manifest_return_code read_optional_string(uint16_t vm_id,
	const struct fdt_node *node, const char *property, struct string *out)
{
	enum manifest_return_code ret;

	ret = read_string(vm_id, node, property, out);
	if (ret == MANIFEST_ERROR_PROPERTY_NOT_FOUND) {
		string_init_empty(out);
		ret = MANIFEST_SUCCESS;
	}
	return ret;
}

enum manifest_return_code read_uint64(uint16_t vm_id, const struct fdt_node *node,
					     const char *property,
					     uint64_t *out)
{
	struct memiter data;

	if (!fdt_read_property(node, property, &data)) {
		dlog_debug("[VM %u] Not found: %s\n", vm_id, property);
		return MANIFEST_ERROR_PROPERTY_NOT_FOUND;
	}

	if (!fdt_parse_number(&data, memiter_size(&data), out)) {
		return MANIFEST_ERROR_MALFORMED_INTEGER;
	}

	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_optional_uint64(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	uint64_t default_value, uint64_t *out)
{
	enum manifest_return_code ret;

	ret = read_uint64(vm_id, node, property, out);
	if (ret == MANIFEST_ERROR_PROPERTY_NOT_FOUND) {
		*out = default_value;
		return MANIFEST_SUCCESS;
	}
	return ret;
}

enum manifest_return_code read_uint32(uint16_t vm_id, const struct fdt_node *node,
					     const char *property,
					     uint32_t *out)
{
	uint64_t value;

	TRY(read_uint64(vm_id, node, property, &value));

	if (value > UINT32_MAX) {
		return MANIFEST_ERROR_INTEGER_OVERFLOW;
	}

	*out = (uint32_t)value;
	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_optional_uint32(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	uint32_t default_value, uint32_t *out)
{
	enum manifest_return_code ret;

	ret = read_uint32(vm_id, node, property, out);
	if (ret == MANIFEST_ERROR_PROPERTY_NOT_FOUND) {
		*out = default_value;
		return MANIFEST_SUCCESS;
	}
	return ret;
}

enum manifest_return_code read_uint16(uint16_t vm_id, const struct fdt_node *node,
					     const char *property,
					     uint16_t *out)
{
	uint64_t value;

	TRY(read_uint64(vm_id, node, property, &value));

	if (value > UINT16_MAX) {
		return MANIFEST_ERROR_INTEGER_OVERFLOW;
	}

	*out = (uint16_t)value;
	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_optional_uint16(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	uint16_t default_value, uint16_t *out)
{
	enum manifest_return_code ret;

	ret = read_uint16(vm_id, node, property, out);
	if (ret == MANIFEST_ERROR_PROPERTY_NOT_FOUND) {
		*out = default_value;
		return MANIFEST_SUCCESS;
	}

	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_uint8(uint16_t vm_id, const struct fdt_node *node,
					    const char *property, uint8_t *out)
{
	uint64_t value;

	TRY(read_uint64(vm_id, node, property, &value));

	if (value > UINT8_MAX) {
		return MANIFEST_ERROR_INTEGER_OVERFLOW;
	}

	*out = (uint8_t)value;
	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_optional_uint8(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	uint8_t default_value, uint8_t *out)
{
	enum manifest_return_code ret;

	ret = read_uint8(vm_id, node, property, out);
	if (ret == MANIFEST_ERROR_PROPERTY_NOT_FOUND) {
		*out = default_value;
		return MANIFEST_SUCCESS;
	}

	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_uint32list(uint16_t vm_id, const struct fdt_node *node,
						 const char *property,
						 struct uint32list_iter *out)
{
	struct memiter data;

	if (!fdt_read_property(node, property, &data)) {
		memiter_init(&out->mem_it, NULL, 0);
		dlog_debug("[VM %u] Not found: %s\n", vm_id, property);
		return MANIFEST_ERROR_PROPERTY_NOT_FOUND;
	}

	if ((memiter_size(&data) % sizeof(uint32_t)) != 0) {
		return MANIFEST_ERROR_MALFORMED_INTEGER_LIST;
	}

	out->mem_it = data;
	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_optional_uint32list(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	struct uint32list_iter *out)
{
	enum manifest_return_code ret = read_uint32list(vm_id, node, property, out);

	if (ret == MANIFEST_ERROR_PROPERTY_NOT_FOUND) {
		return MANIFEST_SUCCESS;
	}
	return ret;
}

bool uint32list_has_next(const struct uint32list_iter *list)
{
	return memiter_size(&list->mem_it) > 0;
}

enum manifest_return_code uint32list_get_next(
	struct uint32list_iter *list, uint32_t *out)
{
	uint64_t num;

	CHECK(uint32list_has_next(list));
	if (!fdt_parse_number(&list->mem_it, sizeof(uint32_t), &num)) {
		return MANIFEST_ERROR_MALFORMED_INTEGER;
	}

	*out = (uint32_t)num;
	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_chararrlist(uint16_t vm_id, const struct fdt_node *node,
						 const char *property,
						 struct chararrlist_iter *out)
{
	struct memiter data;

	if (!fdt_read_property(node, property, &data)) {
		memiter_init(&out->mem_it, NULL, 0);
		dlog_debug("[VM %u] Not found: %s\n", vm_id, property);
		return MANIFEST_ERROR_PROPERTY_NOT_FOUND;
	}

	out->mem_it = data;
	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_optional_chararrlist(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	struct chararrlist_iter *out)
{
	enum manifest_return_code ret = read_chararrlist(vm_id, node, property, out);

	if (ret == MANIFEST_ERROR_PROPERTY_NOT_FOUND) {
		return MANIFEST_SUCCESS;
	}
	return ret;
}

bool chararrlist_has_next(const struct chararrlist_iter *list)
{
	return memiter_size(&list->mem_it) > 0;
}

enum manifest_return_code chararrlist_get_next(
	struct chararrlist_iter *list, char **str_out, size_t *size_out)
{
	CHECK(chararrlist_has_next(list));
	*str_out = (char *) memiter_base(&list->mem_it);
	*size_out = strnlen_s(*str_out, 128);

	if (!memiter_advance(&list->mem_it, *size_out + 1)) {
		return MANIFEST_ERROR_MALFORMED_UUID;
	}
	return MANIFEST_SUCCESS;
}

enum manifest_return_code read_optional_uuid(uint16_t vm_id, const struct fdt_node *node,
		const char *property, uuid_t *out)
{
	struct memiter data;

	if (!fdt_read_property(node, property, &data)) {
		dlog_debug("[VM %u] Not found: %s\n", vm_id, property);
		return MANIFEST_SUCCESS;
	}

	if (!uuid_from_str((char *) memiter_base(&data), memiter_size(&data) - 1, out)) {
		return MANIFEST_ERROR_MALFORMED_UUID;
	}

	return MANIFEST_SUCCESS;
}

