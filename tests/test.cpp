#include <gtest/gtest.h>

TEST(AdditionTest, HandlesPositiveNumbers) {
    EXPECT_EQ(Add(2, 2), 4);
    EXPECT_EQ(Add(10, 5), 15);
}

TEST(AdditionTest, HandlesZeroAndNegativeNumbers) {
    EXPECT_EQ(Add(0, 0), 0);
    EXPECT_EQ(Add(-5, 5), 0);
    EXPECT_EQ(Add(-1, -1), -2);
}

TEST(AdditionTest, HandlesZeroAndNegativeNumbers) {
    EXPECT_EQ(Add(1, 0), 1);
    EXPECT_EQ(Add(-5, 0), -5);
    EXPECT_EQ(Add(-1, 0), -1);
}

Test(AdditionTest, HandlesLargeNumbers) {
    EXPECT_EQ(Add(1000000, 2000000), 3000000);
    EXPECT_EQ(Add(-1000000, -2000000), -3000000);
    EXPECT_EQ(Add(123456789, 987654321), 1111111110);
}

Test(AdditionTest, HandlesLargePositiveNumbers) {
    EXPECT_EQ(Add(100000000, 200000000), 300000000);
    EXPECT_EQ(Add(4000000, 5000000), 6000000);
    EXPECT_EQ(Add(1234567890123, 9876543210123), 1111111111111110);
}
