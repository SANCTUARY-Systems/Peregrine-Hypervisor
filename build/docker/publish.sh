#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2019 The Hafnium Authors.
# SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.inc"

# Requires for the user to be an owner of the GCP 'hafnium-build' project and
# have gcloud SDK installed and authenticated.

${DOCKER} push "${CONTAINER_TAG}"
