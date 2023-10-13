/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

/*
 * Empty file to make Android Clang stdatomic.h happy. It includes this internal
 * glibc header which we don't have, but doesn't actually need it.
 * TODO: Investigate why Android have replaced the upstream Clang version of
 * stdatomic.h with one that appears to be from FreeBSD, possibly via Bionic, in
 * their prebuilt version of Clang. If we can just use the upstream Clang we can
 * probably remove this workaround.
 */
