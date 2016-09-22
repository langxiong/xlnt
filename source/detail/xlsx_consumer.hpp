// Copyright (c) 2014-2016 Thomas Fussell
// Copyright (c) 2010-2015 openpyxl
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, WRISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE
//
// @license: http://www.opensource.org/licenses/mit-license.php
// @author: see AUTHORS file
#pragma once

#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

#include <detail/include_libstudxml.hpp>
#include <xlnt/xlnt_config.hpp>
#include <xlnt/packaging/zip_file.hpp>

namespace xlnt {

class path;
class relationship;
class workbook;

namespace detail {

/// <summary>
/// Handles writing a workbook into an XLSX file.
/// </summary>
class XLNT_CLASS xlsx_consumer
{
public:
	xlsx_consumer(workbook &destination);

	void read(const path &source);

	void read(std::istream &source);

	void read(const std::vector<std::uint8_t> &source);

private:
	/// <summary>
	/// Read all the files needed from the XLSX archive and initialize all of
	/// the data in the workbook to match.
	/// </summary>
	void populate_workbook();

	// Package Parts

	void read_manifest();

	void read_core_properties(xml::parser &parser);
	void read_extended_properties(xml::parser &parser);
	void read_custom_file_properties(xml::parser &parser);

	// SpreadsheetML-Specific Package Parts

	void read_workbook(xml::parser &parser);

	// Workbook Relationship Target Parts

	void read_calculation_chain(xml::parser &parser);
	void read_connections(xml::parser &parser);
	void read_custom_property(xml::parser &parser);
	void read_custom_xml_mappings(xml::parser &parser);
	void read_external_workbook_references(xml::parser &parser);
	void read_metadata(xml::parser &parser);
	void read_pivot_table(xml::parser &parser);
	void read_shared_string_table(xml::parser &parser);
	void read_shared_workbook_revision_headers(xml::parser &parser);
	void read_shared_workbook(xml::parser &parser);
	void read_shared_workbook_user_data(xml::parser &parser);
	void read_stylesheet(xml::parser &parser);
	void read_theme(xml::parser &parser);
	void read_volatile_dependencies(xml::parser &parser);

	void read_chartsheet(const std::string &title, xml::parser &parser);
	void read_dialogsheet(const std::string &title, xml::parser &parser);
	void read_worksheet(const std::string &title, xml::parser &parser);

	// Sheet Relationship Target Parts

	void read_comments(xml::parser &parser);
	void read_drawings(xml::parser &parser);

	// Unknown Parts

	void read_unknown_parts(xml::parser &parser);
	void read_unknown_relationships(xml::parser &parser);

	/// <summary>
	/// A reference to the archive from which files representing the workbook
    /// are read.
	/// </summary>
	zip_file source_;

	std::unordered_map<std::string, std::size_t> sheet_title_id_map_;
	std::unordered_map<std::string, std::size_t> sheet_title_index_map_;

	/// <summary>
	/// A reference to the workbook which is being read.
	/// </summary>
	workbook &destination_;
};

} // namespace detail
} // namespace xlnt
