/* SPDX-FileCopyrightText: 2020 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

/**
 * Print one character to standard output.
 * This is intentionally called differently from functions in <stdio.h> so as to
 * avoid clashes when linking against libc.
 */
void stdout_putchar(char c);
