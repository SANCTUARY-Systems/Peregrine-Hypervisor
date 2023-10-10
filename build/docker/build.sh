#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2019 The Hafnium Authors.
# SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.
# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.inc"

${DOCKER} build \
	--pull \
	-f "${SCRIPT_DIR}/Dockerfile" \
	-t "${CONTAINER_TAG}" \
	"${SCRIPT_DIR}"
