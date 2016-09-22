#pragma once

#include <pugixml.hpp>
#include <sstream>

#include <helpers/path_helper.hpp>
#include <xlnt/packaging/manifest.hpp>

class xml_helper
{
public:
    enum class difference_type
    {
        names_differ,
        missing_attribute,
        attribute_values_differ,
        missing_text,
        text_values_differ,
        missing_child,
        child_order_differs,
        equivalent,
    };
    
    struct comparison_result
    {
        difference_type difference;
        std::string value_left;
        std::string value_right;

        operator bool() const
        {
            return difference == difference_type::equivalent;
        }
    };

    static bool compare_content_types(const pugi::xml_document &left, const pugi::xml_document &right)
    {
        auto left_types_node = left.child("Types");
        auto right_types_node = right.child("Types");
        
        if (!left_types_node || !right_types_node)
        {
            return false;
        }
        
        auto left_children = left_types_node.children();
        auto left_length = std::distance(left_children.begin(), left_children.end());

        auto right_children = right_types_node.children();
        auto right_length = std::distance(right_children.begin(), right_children.end());
        
        if (left_length != right_length)
        {
            return false;
        }

        for (const auto left_child : left_children)
        {
            std::string associated_attribute_name;
            
            if (std::string(left_child.name()) == "Default")
            {
                associated_attribute_name = "Extension";
            }
            else if (std::string(left_child.name()) == "Override")
            {
                associated_attribute_name = "PartName";
            }
            else
            {
                throw std::runtime_error("invalid xml");
            }
            
            std::string left_attribute_value(left_child.attribute(associated_attribute_name.c_str()).value());
            pugi::xml_node matching_right_child;
            
            for (const auto right_child : right_types_node.children(left_child.name()))
            {
                std::string right_attribute_value(right_child.attribute(associated_attribute_name.c_str()).value());
                
                if (left_attribute_value == right_attribute_value)
                {
                    matching_right_child = right_child;
                    break;
                }
            }
            
            if (!matching_right_child
                || !left_child.attribute("ContentType")
                || !matching_right_child.attribute("ContentType"))
            {
                return false;
            }
            
            std::string left_child_type(left_child.attribute("ContentType").value());
            std::string right_child_type(matching_right_child.attribute("ContentType").value());

            if (left_child_type != right_child_type)
            {
                return false;
            }
        }
        
        return true;
    }
    
    static bool compare_relationships_xml(const pugi::xml_document &left, const pugi::xml_document &right)
    {
        auto left_relationships_node = left.child("Relationships");
        auto right_relationships_node = right.child("Relationships");
        
        if (!left_relationships_node || !right_relationships_node)
        {
            return false;
        }
        
        auto left_children = left_relationships_node.children();
        auto left_length = std::distance(left_children.begin(), left_children.end());

        auto right_children = right_relationships_node.children();
        auto right_length = std::distance(right_children.begin(), right_children.end());
        
        if (left_length != right_length)
        {
            return false;
        }

        for (const auto left_child : left_children)
        {
            std::string left_rel_id(left_child.attribute("Id").value());
            pugi::xml_node matching_right_child;
            
            for (const auto right_child : right_children)
            {
                std::string right_rel_id(right_child.attribute("Id").value());
                
                if (left_rel_id == right_rel_id)
                {
                    matching_right_child = right_child;
                    break;
                }
            }
            
            if (!matching_right_child
                || !left_child.attribute("Type")
                || !left_child.attribute("Target")
                || !matching_right_child.attribute("Type")
                || !matching_right_child.attribute("Target"))
            {
                return false;
            }
            
            if ((std::string(left_child.attribute("Type").value())
                    != std::string(left_child.attribute("Type").value()))
                || (std::string(left_child.attribute("Target").value())
                    != std::string(left_child.attribute("Target").value())))
            {
                return false;
            }
        }
        
        return true;
    }
    
    static bool compare_theme_xml(const pugi::xml_document &left, const pugi::xml_document &right)
    {
        return compare_xml_exact(left, right);
    }
    
    static bool compare_styles_xml(const pugi::xml_document &left, const pugi::xml_document &right)
    {
        return compare_xml_exact(left, right);
    }
    
    static bool compare_workbook_xml(const pugi::xml_document &left, const pugi::xml_document &right)
    {
        return compare_xml_exact(left, right);
    }
    
    static bool compare_worksheet_xml(const pugi::xml_document &left, const pugi::xml_document &right)
    {
        return compare_xml_exact(left, right);
    }
    
    static bool compare_core_properties_xml(const pugi::xml_document &left, const pugi::xml_document &right)
    {
        return compare_xml_exact(left, right);
    }
    
    static bool compare_extended_properties_xml(const pugi::xml_document &left, const pugi::xml_document &right)
    {
        return compare_xml_exact(left, right);
    }
    
    static bool compare_files(const std::string &left,
		const std::string &right, const std::string &content_type)
    {
        auto is_xml = (content_type.substr(0, 12) == "application/"
            && content_type.substr(content_type.size() - 4) == "+xml")
            || content_type == "application/xml"
            || content_type == "[Content_Types].xml";
        
        if (is_xml)
        {
            pugi::xml_document left_document;
            left_document.load(left.c_str());

            pugi::xml_document right_document;
            right_document.load(right.c_str());
            
            if (content_type == "[Content_Types].xml")
            {
                return compare_content_types(left_document, right_document);
            }
            else if (content_type == "application/vnd.openxmlformats-package.relationships+xml")
            {
                return compare_relationships_xml(left_document, right_document);
            }
            else if (content_type == "application/vnd.openxmlformats-officedocument.theme+xml")
            {
                return compare_theme_xml(left_document, right_document);
            }
            else if (content_type == "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml")
            {
                return compare_styles_xml(left_document, right_document);
            }
            else if (content_type == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml")
            {
                return compare_workbook_xml(left_document, right_document);
            }
            else if (content_type == "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml")
            {
                return compare_worksheet_xml(left_document, right_document);
            }
            else if (content_type == "application/vnd.openxmlformats-package.core-properties+xml")
            {
                return compare_core_properties_xml(left_document, right_document);
            }
            else if (content_type == "application/vnd.openxmlformats-officedocument.extended-properties+xml")
            {
                return compare_extended_properties_xml(left_document, right_document);
            }
            
            return compare_xml_exact(left_document, right_document);
        }
        
        return left == right;
    }

    static bool compare_xml_exact(const pugi::xml_document &left, const pugi::xml_document &right)
    {
        auto result = compare_xml_nodes(left, right);

		if (!result)
		{
			std::cout << "documents don't match" << std::endl;

			std::cout << "left:" << std::endl;
			left.save(std::cout);
			std::cout << std::endl;

			std::cout << "right:" << std::endl;
			right.save(std::cout);
			std::cout << std::endl;

			return false;
		}

		return true;
    }

	static bool string_matches_workbook_part(const std::string &expected,
		xlnt::workbook &wb, const xlnt::path &part, const std::string &content_type)
	{
		std::vector<std::uint8_t> bytes;
		wb.save(bytes);
		xlnt::zip_file archive;
		archive.load(bytes);

		return string_matches_archive_member(expected, archive, part, content_type);
	}

	static bool file_matches_workbook_part(const xlnt::path &expected,
		xlnt::workbook &wb, const xlnt::path &part, const std::string &content_type)
	{
		std::vector<std::uint8_t> bytes;
		wb.save(bytes);
		xlnt::zip_file archive;
		archive.load(bytes);

		return file_matches_archive_member(expected, archive, part, content_type);
	}

	static bool string_matches_archive_member(const std::string &expected,
		xlnt::zip_file &archive,
		const xlnt::path &member,
        const std::string &content_type)
	{
		return compare_files(expected, archive.read(member), content_type);
	}

	static bool file_matches_archive_member(const xlnt::path &file,
		xlnt::zip_file &archive,
		const xlnt::path &member,
        const std::string &content_type)
	{
		if (!archive.has_file(member)) return false;
		return compare_files(file.read_contents(), archive.read(member), content_type);
	}

	static bool file_matches_document(const xlnt::path &expected, 
		const pugi::xml_document &observed, const std::string &content_type)
	{
		return string_matches_document(expected.read_contents(), observed, content_type);
	}

    static bool string_matches_document(const std::string &string,
		const pugi::xml_document &document, const std::string &content_type)
    {        
        std::ostringstream ss;
        document.save(ss);

        return compare_files(string, ss.str(), content_type);
    }

	static bool xlsx_archives_match(xlnt::zip_file &left, xlnt::zip_file &right)
	{
		const auto left_info = left.infolist();
		const auto right_info = right.infolist();

		if (left_info.size() != right_info.size())
        {
            std::cout << "left has a different number of files than right" << std::endl;

            std::cout << "left has: ";
            for (auto &info : left_info)
            {
                std::cout << info.filename.string() << ", ";
            }
            std::cout << std::endl;

            std::cout << "right has: ";
            for (auto &info : right_info)
            {
                std::cout << info.filename.string() << ", ";
            }
            std::cout << std::endl;
        }
        
        bool match = true;
        std::vector<std::uint8_t> buffer;
        
        left.save(buffer);
        xlnt::workbook left_workbook;
        left_workbook.load(buffer);
        
        buffer.clear();
        
        right.save(buffer);
        xlnt::workbook right_workbook;
        right_workbook.load(buffer);
        
        auto &left_manifest = left_workbook.get_manifest();
        auto &right_manifest = right_workbook.get_manifest();

		for (auto left_member : left_info)
		{
			if (!right.has_file(left_member))
            {
                match = false;
                std::cout << "right is missing file: " << left_member.filename.string() << std::endl;
                continue;
            }

			auto left_member_contents = left.read(left_member);
			auto right_member_contents = right.read(left_member.filename);
            
            std::string left_content_type, right_content_type;
            
            if (left_member.filename.string() != "[Content_Types].xml")
            {
                left_content_type = left_manifest.get_content_type(xlnt::path(left_member.filename.string()));
                right_content_type = right_manifest.get_content_type(xlnt::path(left_member.filename.string()));
            }
            else
            {
                left_content_type = right_content_type = "[Content_Types].xml";
            }
            
            if (left_content_type != right_content_type)
            {
                std::cout << "content types differ: "
                    << left_member.filename.string()
                    << " "
                    << left_content_type
                    << " "
                    << right_content_type
                    << std::endl;
                match = false;
            }
            else if (!compare_files(left_member_contents, right_member_contents, left_content_type))
			{
				std::cout << left_member.filename.string() << std::endl;
                match = false;
			}
		}

		return match;
	}
    
    static comparison_result compare_xml_nodes(const pugi::xml_node &left, const pugi::xml_node &right)
    {
        std::string left_name(left.name());
        std::string right_name(right.name());
        
        if(left_name != right_name)
        {
            return {difference_type::names_differ, left_name, right_name};
        }
        
        for(const auto &left_attribute : left.attributes())
        {
            std::string attribute_name(left_attribute.name());
            auto right_attribute = right.attribute(attribute_name.c_str());
            
            // pugixml doesn't handle namespaces correctly
            bool special_exception = left_name == "mc:AlternateContent"
                && attribute_name == "xmlns:mc";

            if(!right_attribute && !special_exception)
            {
                return {difference_type::missing_attribute, attribute_name, "((empty))"};
            }
            
            std::string left_attribute_value(left_attribute.value());
            std::string right_attribute_value(right_attribute.value());
            
            if(left_attribute_value != right_attribute_value && !special_exception)
            {
                return {difference_type::attribute_values_differ, left_attribute_value, right_attribute_value};
            }
        }

        for(const auto &right_attribute : right.attributes())
        {
            std::string attribute_name(right_attribute.name());
            auto left_attribute = left.attribute(attribute_name.c_str());

            // pugixml doesn't handle namespaces correctly
            bool special_exception = left_name == "mc:AlternateContent"
                && attribute_name == "xmlns:mc";

            if(!left_attribute && !special_exception)
            {
                return {difference_type::missing_attribute, "((empty))", attribute_name};
            }
            
            std::string left_attribute_value(left_attribute.value());
            std::string right_attribute_value(right_attribute.value());
            
            if(left_attribute_value != right_attribute_value && !special_exception)
            {
                return {difference_type::attribute_values_differ, left_attribute_value, right_attribute_value};
            }
        }
        
        if(left.text())
        {
            std::string left_text(left.text().get());
            
            if(!right.text())
            {
                return {difference_type::missing_text, left_text, "((empty))"};
            }
            
            std::string right_text(right.text().get());
            
            if(left_text != right_text)
            {
                return {difference_type::text_values_differ, left_text, right_text};
            }
        }
        else if(right.text())
        {
            return {difference_type::text_values_differ, "((empty))", right.text().get()};
        }
        
        auto right_children = right.children();
        auto right_child_iter = right_children.begin();
        
        for(auto left_child : left.children())
        {
            std::string left_child_name(left_child.name());
            
            if(right_child_iter == right_children.end())
            {
                return {difference_type::child_order_differs, left_child_name, "((end))"};
            }
            
            auto right_child = *right_child_iter;
            right_child_iter++;
            
            auto child_comparison_result = compare_xml_nodes(left_child, right_child);
            
            if(!child_comparison_result)
            {
                return child_comparison_result;
            }
        }
        
        if(right_child_iter != right_children.end())
        {
            return {difference_type::child_order_differs, "((end))", right_child_iter->name()};
        }
        
        return {difference_type::equivalent, "", ""};
    }
};
