#pragma once
#ifndef EXTERNAL_INCLUDE
#include "pg/uuid.h"
#else
#include <kernel/fdt.h>
#include <tee_api_types.h>
#endif

struct uint32list_iter {
	struct memiter mem_it;
};

enum manifest_return_code read_bool(uint16_t,const struct fdt_node *,
					   const char *, bool *);

enum manifest_return_code read_optional_char_arr(uint16_t vm_id, const struct fdt_node *node,
		const char *property, struct string_bundle *out);

enum manifest_return_code read_string(uint16_t vm_id, const struct fdt_node *node,
					     const char *property, struct string *out);


enum manifest_return_code read_optional_string(uint16_t vm_id,
	const struct fdt_node *node, const char *property, struct string *out);

enum manifest_return_code read_uint64(uint16_t vm_id, const struct fdt_node *node,
					     const char *property,
					     uint64_t *out);

enum manifest_return_code read_optional_uint64(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	uint64_t default_value, uint64_t *out);

enum manifest_return_code read_uint32(uint16_t vm_id, const struct fdt_node *node,
					     const char *property,
					     uint32_t *out);

enum manifest_return_code read_optional_uint32(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	uint32_t default_value, uint32_t *out);

enum manifest_return_code read_uint16(uint16_t vm_id, const struct fdt_node *node,
					     const char *property,
					     uint16_t *out);

enum manifest_return_code read_optional_uint16(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	uint16_t default_value, uint16_t *out);

enum manifest_return_code read_uint8(uint16_t vm_id, const struct fdt_node *node,
					    const char *property, uint8_t *out);

enum manifest_return_code read_optional_uint8(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	uint8_t default_value, uint8_t *out);

enum manifest_return_code read_uint32list(uint16_t vm_id, const struct fdt_node *node,
						 const char *property,
						 struct uint32list_iter *out);

enum manifest_return_code read_optional_uint32list(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	struct uint32list_iter *out);

bool uint32list_has_next(const struct uint32list_iter *list);

enum manifest_return_code uint32list_get_next(
	struct uint32list_iter *list, uint32_t *out);

enum manifest_return_code read_optional_char_arr_sh(uint16_t vm_id, const struct fdt_node *node,
		const char *property, struct string_bundle_sh *out);

struct chararrlist_iter {
	struct memiter mem_it;
};

enum manifest_return_code read_chararrlist(uint16_t vm_id, const struct fdt_node *node,
						 const char *property,
						 struct chararrlist_iter *out);

enum manifest_return_code read_optional_chararrlist(uint16_t vm_id,
	const struct fdt_node *node, const char *property,
	struct chararrlist_iter *out);

bool chararrlist_has_next(const struct chararrlist_iter *list);

enum manifest_return_code chararrlist_get_next(struct chararrlist_iter *list, char **str_out, size_t *size_out);

enum manifest_return_code read_optional_uuid(uint16_t vm_id, const struct fdt_node *node,
		const char *property, uuid_t *out);
