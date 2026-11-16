#include <gtest/gtest.h>

#include "nexus/common/result.hpp"
#include "nexus/common/status.hpp"

namespace nexus {
namespace {

TEST(StatusTest, OkStatus) {
    Status s = Status::Ok();
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(s.code(), StatusCode::Ok);
    EXPECT_EQ(s.message(), "");
    EXPECT_EQ(s.ToString(), "OK");
}

TEST(StatusTest, ErrorStatuses) {
    Status not_found = Status::NotFound("Table users not found");
    EXPECT_FALSE(not_found.ok());
    EXPECT_EQ(not_found.code(), StatusCode::NotFound);
    EXPECT_EQ(not_found.message(), "Table users not found");
    EXPECT_EQ(not_found.ToString(), "NOT_FOUND: Table users not found");

    Status corruption = Status::Corruption("Checksum mismatch on page 42");
    EXPECT_FALSE(corruption.ok());
    EXPECT_EQ(corruption.code(), StatusCode::Corruption);

    Status out_of_range = Status::OutOfRange("Offset past end of page");
    EXPECT_FALSE(out_of_range.ok());
    EXPECT_EQ(out_of_range.code(), StatusCode::OutOfRange);

    Status io_error = Status::IoError("Disk read failure");
    EXPECT_FALSE(io_error.ok());
    EXPECT_EQ(io_error.code(), StatusCode::IoError);

    Status buf_exhausted = Status::BufferPoolExhausted("All frames pinned");
    EXPECT_FALSE(buf_exhausted.ok());
    EXPECT_EQ(buf_exhausted.code(), StatusCode::BufferPoolExhausted);
}

TEST(StatusTest, EqualityComparison) {
    Status s1 = Status::NotFound("missing");
    Status s2 = Status::NotFound("missing");
    Status s3 = Status::NotFound("other");
    Status s4 = Status::InvalidArgument("missing");

    EXPECT_EQ(s1, s2);
    EXPECT_NE(s1, s3);
    EXPECT_NE(s1, s4);
}

TEST(ResultTest, SuccessValue) {
    Result<int> res(42);
    EXPECT_TRUE(res.ok());
    EXPECT_EQ(res.status(), Status::Ok());
    EXPECT_EQ(res.value(), 42);
    EXPECT_EQ(*res, 42);

    Result<std::string> str_res(std::string("NexusDB"));
    EXPECT_TRUE(str_res.ok());
    EXPECT_EQ(*str_res, "NexusDB");
}

TEST(ResultTest, FailureStatus) {
    Result<int> res(Status::NotFound("Value not present"));
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status().code(), StatusCode::NotFound);
    EXPECT_EQ(res.status().message(), "Value not present");

    EXPECT_THROW([[maybe_unused]] auto val = res.value(), std::runtime_error);
}

TEST(ResultTest, MoveSemantics) {
    std::string text = "Persistent Storage Engine";
    Result<std::string> res(std::move(text));
    EXPECT_TRUE(res.ok());
    EXPECT_EQ(res.value(), "Persistent Storage Engine");

    std::string extracted = std::move(res).value();
    EXPECT_EQ(extracted, "Persistent Storage Engine");
}

} // namespace
} // namespace nexus
