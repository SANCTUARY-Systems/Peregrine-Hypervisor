/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

/**
 * Disables interrupts.
 */
void arch_irq_disable(void);

/**
 * Enables interrupts.
 */
void arch_irq_enable(void);
