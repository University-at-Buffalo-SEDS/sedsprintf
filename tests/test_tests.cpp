#include <gtest/gtest.h>

int Add(int a, int b) {
    return a + b;
}

TEST(AdditionTest, HandlesPositiveNumbers) {
    EXPECT_EQ(Add(2, 2), 4);
    EXPECT_EQ(Add(10, 5), 15);
}

TEST(AdditionTest, HandlesZeroAndNegativeNumbers) {
    EXPECT_EQ(Add(0, 0), 0);
    EXPECT_EQ(Add(-5, 5), 0);
    EXPECT_EQ(Add(-1, -1), -2);
}