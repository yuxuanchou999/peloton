/*-------------------------------------------------------------------------
 *
 * export_serializer.h
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /n-store/src/common/export_serializer.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include <cassert>
#include <cstdio>

namespace nstore {

/*
 * This file defines a crude Export serialization interface.
 * The idea is that other code could implement these method
 * names and duck-type their way to a different Export
 * serialization .. maybe doing some dynamic symbol finding
 * for a pluggable Export serializer. It's a work in progress.
 *
 * This doesn't derive from serializeio to avoid making the
 * the serialize IO baseclass functions all virtual.
 */

class ExportSerializeInput {
public:

	ExportSerializeInput(const void* data, size_t length)
{
		current_ = reinterpret_cast<const char*>(data);
		end_ = current_ + length;
}


	virtual ~ExportSerializeInput() {};

	inline char readChar() {
		return readPrimitive<char>();
	}

	inline int8_t readByte() {
		return readPrimitive<int8_t>();
	}

	inline int16_t readShort() {
		return readPrimitive<int16_t>();
	}

	inline int32_t readInt() {
		return readPrimitive<int32_t>();
	}

	inline bool readBool() {
		return readByte();
	}

	inline char readEnumInSingleByte() {
		return readByte();
	}

	inline int64_t readLong() {
		return readPrimitive<int64_t>();
	}

	inline float readFloat() {
		int32_t value = readPrimitive<int32_t>();
		float retval;
		memcpy(&retval, &value, sizeof(retval));
		return retval;
	}

	inline double readDouble() {
		int64_t value = readPrimitive<int64_t>();
		double retval;
		memcpy(&retval, &value, sizeof(retval));
		return retval;
	}

	/** Returns a pointer to the internal data buffer, advancing the read position by length. */
	const void* getRawPointer(size_t length) {
		const void* result = current_;
		current_ += length;
		assert(current_ <= end_);
		return result;
	}

	/** Copy a string from the buffer. */
	inline std::string readTextString() {
		int32_t stringLength = readInt();
		assert(stringLength >= 0);
		return std::string(reinterpret_cast<const char*>(getRawPointer(stringLength)),
				stringLength);
	};

	/** Copy the next length bytes from the buffer to destination. */
	inline void readBytes(void* destination, size_t length) {
		::memcpy(destination, getRawPointer(length), length);
	};

	/** Move the read position back by bytes. Warning: this method is
    currently unverified and could result in reading before the
    beginning of the buffer. */
	// TODO(evanj): Change the implementation to validate this?
	void unread(size_t bytes) {
		current_ -= bytes;
	}

private:
	template <typename T>
	T readPrimitive() {
		T value;
		::memcpy(&value, current_, sizeof(value));
		current_ += sizeof(value);
		return value;
	}

	// Current read position.
	const char* current_;

	// End of the buffer. Valid byte range: current_ <= validPointer < end_.
	const char* end_;

	// No implicit copies
	ExportSerializeInput(const ExportSerializeInput&);
	ExportSerializeInput& operator=(const ExportSerializeInput&);
};

class ExportSerializeOutput {
public:
	ExportSerializeOutput(void *buffer, size_t capacity) :
		buffer_(NULL), position_(0), capacity_(0)
{
		buffer_ = reinterpret_cast<char*>(buffer);
		assert(position_ <= capacity);
		capacity_ = capacity;
}

	virtual ~ExportSerializeOutput() {
		// the serialization wrapper never owns its data buffer
	};

	/** Returns a pointer to the beginning of the buffer, for reading the serialized data. */
	const char* data() const { return buffer_; }

	/** Returns the number of bytes written in to the buffer. */
	size_t size() const { return position_; }

	// functions for serialization
	inline void writeChar(char value) {
		writePrimitive(value);
	}

	inline void writeByte(int8_t value) {
		writePrimitive(value);
	}

	inline void writeShort(int16_t value) {
		writePrimitive(static_cast<uint16_t>(value));
	}

	inline void writeInt(int32_t value) {
		writePrimitive(value);
	}

	inline void writeBool(bool value) {
		writeByte(value ? int8_t(1) : int8_t(0));
	};

	inline void writeLong(int64_t value) {
		writePrimitive(value);
	}

	inline void writeFloat(float value) {
		int32_t data;
		memcpy(&data, &value, sizeof(data));
		writePrimitive(data);
	}

	inline void writeDouble(double value) {
		int64_t data;
		memcpy(&data, &value, sizeof(data));
		writePrimitive(data);
	}

	inline void writeEnumInSingleByte(int value) {
		assert(std::numeric_limits<int8_t>::min() <= value &&
				value <= std::numeric_limits<int8_t>::max());
		writeByte(static_cast<int8_t>(value));
	}

	// this explicitly accepts char* and length (or ByteArray)
	// as std::string's implicit construction is unsafe!
	inline void writeBinaryString(const void* value, size_t length) {
		int32_t stringLength = static_cast<int32_t>(length);
		assureExpand(length + sizeof(stringLength));

		char* current = buffer_ + position_;
		memcpy(current, &stringLength, sizeof(stringLength));
		current += sizeof(stringLength);
		memcpy(current, value, length);
		position_ += sizeof(stringLength) + length;
	}

	//inline void writeTextString(const std::string &value) {
	//	writeBinaryString(value.data(), value.size());
	//}

	inline void writeBytes(const void *value, size_t length) {
		assureExpand(length);
		memcpy(buffer_ + position_, value, length);
		position_ += length;
	}

	inline void writeZeros(size_t length) {
		assureExpand(length);
		memset(buffer_ + position_, 0, length);
		position_ += length;
	}

	/** Reserves length bytes of space for writing. Returns the offset to the bytes. */
	size_t reserveBytes(size_t length) {
		assureExpand(length);
		size_t offset = position_;
		position_ += length;
		return offset;
	}

	std::size_t position() {
		return position_;
	}

	void position(std::size_t pos) {
		position_ = pos;
	}

private:
	template <typename T>
	void writePrimitive(T value) {
		assureExpand(sizeof(value));
		memcpy(buffer_ + position_, &value, sizeof(value));
		position_ += sizeof(value);
	}

	template <typename T>
	size_t writePrimitiveAt(size_t position, T value) {
		return writeBytesAt(position, &value, sizeof(value));
	}

	inline void assureExpand(size_t next_write) {
		size_t minimum_desired = position_ + next_write;
		if (minimum_desired > capacity_) {
			// TODO: die
		}
		assert(capacity_ >= minimum_desired);
	}

	// Beginning of the buffer.
	char* buffer_;

	// No implicit copies
	ExportSerializeOutput(const ExportSerializeOutput&);
	ExportSerializeOutput& operator=(const ExportSerializeOutput&);

protected:
	// Current write position in the buffer.
	size_t position_;
	// Total bytes this buffer can contain.
	size_t capacity_;
};

}
