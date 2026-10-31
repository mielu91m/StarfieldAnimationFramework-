#pragma once

#include <string>
#include <string_view>

namespace Animation
{
	class FileID
	{
	public:
		FileID() = default;
		FileID(std::string_view a_filePath, std::string_view a_id);

		std::string_view QPath() const;
		std::string_view QID() const;

		bool operator==(const FileID& a_rhs) const;
		bool operator<(const FileID& a_rhs) const;

	private:
		std::string _filePath;
		std::string _id;
	};

	struct AnimID
	{
		FileID file;
		std::string skeleton;

		bool operator==(const AnimID& other) const;
		bool operator<(const AnimID& other) const;
	};
}
