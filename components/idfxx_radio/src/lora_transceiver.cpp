// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/radio/lora_transceiver>

// Out-of-line anchor for the abstract base's vtable. Keeps the vtable and
// type-info in a single translation unit so concrete drivers don't each emit
// their own copy.
namespace idfxx::radio {

// No method bodies needed: every virtual is pure and the destructor is
// defaulted in the header. This file exists only to give the class a single
// home translation unit.

} // namespace idfxx::radio
