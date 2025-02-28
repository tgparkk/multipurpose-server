#pragma once

class SendBufferChunk;

extern thread_local uint32 LThreadId;
extern thread_local std::shared_ptr<SendBufferChunk> LSendBufferChunk;