#include "CppUTest/TestHarness.h"

extern "C" {
#include "sis.h"
}

TEST_GROUP(UartTestGroup)
{
};

TEST(UartTestGroup, CheckIfTestsAreWorking)
{
   FAIL("As it should");
}