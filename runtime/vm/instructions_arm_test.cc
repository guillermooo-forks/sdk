// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_ARM)

#include "vm/assembler.h"
#include "vm/instructions.h"
#include "vm/stub_code.h"
#include "vm/unit_test.h"

namespace dart {

#define __ assembler->

ASSEMBLER_TEST_GENERATE(Call, assembler) {
  __ BranchLinkPatchable(&StubCode::InstanceFunctionLookupLabel());
  // Do not emit any code after the call above, since we decode backwards
  // starting from the return address, i.e. from the end of this code buffer.
}


ASSEMBLER_TEST_RUN(Call, test) {
  CallPattern call(test->entry() + test->code().Size(), test->code());
  EXPECT_EQ(StubCode::InstanceFunctionLookupLabel().address(),
            call.TargetAddress());
}


ASSEMBLER_TEST_GENERATE(Jump, assembler) {
  UNIMPLEMENTED();
}


ASSEMBLER_TEST_RUN(Jump, test) {
  JumpPattern jump1(test->entry());
  EXPECT_EQ(StubCode::InstanceFunctionLookupLabel().address(),
            jump1.TargetAddress());
  JumpPattern jump2(test->entry() + jump1.pattern_length_in_bytes());
  EXPECT_EQ(StubCode::AllocateArrayLabel().address(),
            jump2.TargetAddress());
  uword target1 = jump1.TargetAddress();
  uword target2 = jump2.TargetAddress();
  jump1.SetTargetAddress(target2);
  jump2.SetTargetAddress(target1);
  EXPECT_EQ(StubCode::AllocateArrayLabel().address(),
            jump1.TargetAddress());
  EXPECT_EQ(StubCode::InstanceFunctionLookupLabel().address(),
            jump2.TargetAddress());
}

}  // namespace dart

#endif  // defined TARGET_ARCH_ARM