#include "CppUTest/TestHarness.h"

extern "C" {
#include "sis.h"
}

TEST_GROUP(GeneralTests)
{
};

TEST(GeneralTests, ShouldExecQuitCommandAndReturnQuitStatus)
{
    CHECK_EQUAL(QUIT, exec_cmd("quit"));
}
