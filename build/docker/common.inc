# SPDX-FileCopyrightText: 2019 The Hafnium Authors.
# SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.
# SPDX-License-Identifier: GPL-2.0-only

CONTAINER_TAG="eu.gcr.io/hafnium-build/hafnium_ci"

if [[ ! -v DOCKER ]]
then
	DOCKER="$(which docker)" \
		|| (echo "ERROR: Could not find Docker binary" 1>&2; exit 1)
fi