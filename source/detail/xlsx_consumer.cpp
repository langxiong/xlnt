#include <cctype>

#include <detail/xlsx_consumer.hpp>

#include <detail/constants.hpp>
#include <detail/custom_value_traits.hpp>
#include <detail/workbook_impl.hpp>
#include <xlnt/cell/cell.hpp>
#include <xlnt/utils/path.hpp>
#include <xlnt/packaging/manifest.hpp>
#include <xlnt/packaging/zip_file.hpp>
#include <xlnt/workbook/const_worksheet_iterator.hpp>
#include <xlnt/workbook/workbook.hpp>
#include <xlnt/worksheet/worksheet.hpp>

namespace {

bool is_true(const std::string &bool_string)
{
	return bool_string == "1" || bool_string == "true";
}

std::size_t string_to_size_t(const std::string &s)
{
#if ULLONG_MAX == SIZE_MAX
	return std::stoull(s);
#else
	return std::stoul(s);
#endif
}

xlnt::datetime w3cdtf_to_datetime(const std::string &string)
{
	xlnt::datetime result(1900, 1, 1);
	auto separator_index = string.find('-');
	result.year = std::stoi(string.substr(0, separator_index));
	result.month = std::stoi(string.substr(separator_index + 1, string.find('-', separator_index + 1)));
	separator_index = string.find('-', separator_index + 1);
	result.day = std::stoi(string.substr(separator_index + 1, string.find('T', separator_index + 1)));
	separator_index = string.find('T', separator_index + 1);
	result.hour = std::stoi(string.substr(separator_index + 1, string.find(':', separator_index + 1)));
	separator_index = string.find(':', separator_index + 1);
	result.minute = std::stoi(string.substr(separator_index + 1, string.find(':', separator_index + 1)));
	separator_index = string.find(':', separator_index + 1);
	result.second = std::stoi(string.substr(separator_index + 1, string.find('Z', separator_index + 1)));
	return result;
}

/*
struct EnumClassHash
{
	template <typename T>
	std::size_t operator()(T t) const
	{
		return static_cast<std::size_t>(t);
	}
};
*/

xlnt::protection read_protection(xml::parser &parser)
{
    parser.next_expect(xml::parser::event_type::start_element, "protection");
    
	xlnt::protection prot;
    prot.locked(is_true(parser.attribute("locked")));
    prot.hidden(is_true(parser.attribute("hidden")));
    
    parser.next_expect(xml::parser::event_type::end_element, "protection");

	return prot;
}

xlnt::alignment read_alignment(xml::parser &parser)
{
	xlnt::alignment align;

	align.wrap(is_true(parser.attribute("wrapText")));
	align.shrink(is_true(parser.attribute("shrinkToFit")));

	if (parser.attribute_present("vertical"))
	{
		align.vertical(parser.attribute<xlnt::vertical_alignment>("vertical"));
	}

	if (parser.attribute_present("horizontal"))
	{
		align.horizontal(parser.attribute<xlnt::horizontal_alignment>("horizontal"));
	}

	return align;
}

xlnt::color read_color(xml::parser &parser)
{
	xlnt::color result;

	if (parser.attribute_present("auto"))
	{
		return result;
	}

	if (parser.attribute_present("rgb"))
	{
		result = xlnt::rgb_color(parser.attribute("rgb"));
	}
	else if (parser.attribute_present("theme"))
	{
		result = xlnt::theme_color(string_to_size_t(parser.attribute("theme")));
	}
	else if (parser.attribute_present("indexed"))
	{
		result = xlnt::indexed_color(string_to_size_t(parser.attribute("indexed")));
	}

	if (parser.attribute_present("tint"))
	{
		result.set_tint(parser.attribute("tint", 0.0));
	}

	return result;
}

xlnt::font read_font(xml::parser &parser)
{
    static const auto xmlns = xlnt::constants::get_namespace("worksheet");

	xlnt::font new_font;
    
    parser.next_expect(xml::parser::event_type::start_element, xmlns, "font");
    parser.content(xml::parser::content_type::complex);

    while (true)
    {
        if (parser.peek() == xml::parser::event_type::end_element) break;
        
        parser.next_expect(xml::parser::event_type::start_element);
        parser.content(xml::parser::content_type::simple);
        
        if (parser.name() == "sz")
        {
            new_font.size(string_to_size_t(parser.attribute("val")));
        }
        else if (parser.name() == "name")
        {
            new_font.name(parser.attribute("val"));
        }
        else if (parser.name() == "color")
        {
            new_font.color(read_color(parser));
        }
        else if (parser.name() == "family")
        {
            new_font.family(string_to_size_t(parser.attribute("val")));
        }
        else if (parser.name() == "scheme")
        {
            new_font.scheme(parser.attribute("val"));
        }
        else if (parser.name() == "b")
        {
            if (parser.attribute_present("val"))
            {
                new_font.bold(is_true(parser.attribute("val")));
            }
            else
            {
                new_font.bold(true);
            }
        }
        else if (parser.name() == "strike")
        {
            if (parser.attribute_present("val"))
            {
                new_font.strikethrough(is_true(parser.attribute("val")));
            }
            else
            {
                new_font.strikethrough(true);
            }
        }
        else if (parser.name() == "i")
        {
            if (parser.attribute_present("val"))
            {
                new_font.italic(is_true(parser.attribute("val")));
            }
            else
            {
                new_font.italic(true);
            }
        }
        else if (parser.name() == "u")
        {
            if (parser.attribute_present("val"))
            {
                new_font.underline(parser.attribute<xlnt::font::underline_style>("val"));
            }
            else
            {
                new_font.underline(xlnt::font::underline_style::single);
            }
        }
        
        parser.next_expect(xml::parser::event_type::end_element);
    }
    
    parser.next_expect(xml::parser::event_type::end_element, xmlns, "font");

	return new_font;
}

void read_indexed_colors(xml::parser &parser, std::vector<xlnt::color> &colors)
{
	colors.clear();

    while (true)
    {
        if (parser.peek() == xml::parser::event_type::end_element)
        {
            break;
        }

		colors.push_back(read_color(parser));
	}
    
    parser.next_expect(xml::parser::event_type::end_element, "indexedColors");
}

xlnt::fill read_fill(xml::parser &parser)
{
    static const auto xmlns = xlnt::constants::get_namespace("worksheet");

	xlnt::fill new_fill;
    
    parser.next_expect(xml::parser::event_type::start_element, xmlns, "fill");
    parser.content(xml::parser::content_type::complex);
    parser.next_expect(xml::parser::event_type::start_element);
    
	if (parser.qname() == xml::qname(xmlns, "patternFill"))
	{
		xlnt::pattern_fill pattern;

		if (parser.attribute_present("patternType"))
		{
			pattern.type(parser.attribute<xlnt::pattern_fill_type>("patternType"));
            
            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element)
                {
                    break;
                }
                
                parser.next_expect(xml::parser::event_type::start_element);
            
                if (parser.name() == "fgColor")
                {
                    pattern.foreground(read_color(parser));
                }
                else if (parser.name() == "bgColor")
                {
                    pattern.background(read_color(parser));
                }
                
                parser.next_expect(xml::parser::event_type::end_element);
            }
		}

		new_fill = pattern;
	}
	else if (parser.qname() == xml::qname(xmlns, "gradientFill"))
	{
		xlnt::gradient_fill gradient;

		if (parser.attribute_present("type"))
		{
			gradient.type(parser.attribute<xlnt::gradient_fill_type>("type"));
		}
		else
		{
			gradient.type(xlnt::gradient_fill_type::linear);
		}

        while (true)
        {
            if (parser.peek() == xml::parser::event_type::end_element) break;
            
            parser.next_expect(xml::parser::event_type::start_element, "stop");
			auto position = parser.attribute<double>("position");
            parser.next_expect(xml::parser::event_type::start_element, "color");
			auto color = read_color(parser);
            parser.next_expect(xml::parser::event_type::end_element, "color");
            parser.next_expect(xml::parser::event_type::end_element, "stop");

			gradient.add_stop(position, color);
		}

		new_fill = gradient;
	}
    
    parser.next_expect(xml::parser::event_type::end_element); // </gradientFill> or </patternFill>
    parser.next_expect(xml::parser::event_type::end_element); // </fill>

	return new_fill;
}

xlnt::border::border_property read_side(xml::parser &parser)
{
	xlnt::border::border_property new_side;
    
	if (parser.attribute_present("style"))
	{
		new_side.style(parser.attribute<xlnt::border_style>("style"));
	}

    if (parser.peek() == xml::parser::event_type::start_element)
    {
        parser.next_expect(xml::parser::event_type::start_element, "color");
		new_side.color(read_color(parser));
        parser.next_expect(xml::parser::event_type::end_element, "color");
	}

	return new_side;
}

xlnt::border read_border(xml::parser &parser)
{
	xlnt::border new_border;
    
    parser.next_expect(xml::parser::event_type::start_element); // <border>
    parser.content(xml::parser::content_type::complex);
    
    while (true)
    {
        if (parser.peek() == xml::parser::event_type::end_element) break;

        parser.next_expect(xml::parser::event_type::start_element);
        auto side_type = xml::value_traits<xlnt::border_side>::parse(parser.name(), parser);
        auto side = read_side(parser);
        new_border.side(side_type, side);
        parser.next_expect(xml::parser::event_type::end_element);
    }
    
    parser.next_expect(xml::parser::event_type::end_element); // </border>

	return new_border;
}

std::vector<xlnt::relationship> read_relationships(const xlnt::path &part, xlnt::zip_file &archive)
{
	std::vector<xlnt::relationship> relationships;
	if (!archive.has_file(part)) return relationships;

    std::istringstream rels_stream(archive.read(part));
    xml::parser parser(rels_stream, part.string());

    xlnt::uri source(part.string());

    const auto xmlns = xlnt::constants::get_namespace("relationships");
    parser.next_expect(xml::parser::event_type::start_element, xmlns, "Relationships");
    parser.content(xml::content::complex);

    while (true)
	{
        if (parser.peek() == xml::parser::event_type::end_element) break;
        
        parser.next_expect(xml::parser::event_type::start_element, xmlns, "Relationship");
		relationships.emplace_back(parser.attribute("Id"),
            parser.attribute<xlnt::relationship::type>("Type"), source,
            xlnt::uri(parser.attribute("Target")), xlnt::target_mode::internal);
        parser.next_expect(xml::parser::event_type::end_element,
            xlnt::constants::get_namespace("relationships"), "Relationship");
	}
    
    parser.next_expect(xml::parser::event_type::end_element, xmlns, "Relationships");

	return relationships;
}

void check_document_type(const std::string &document_content_type)
{
	if (document_content_type != "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"
		&& document_content_type != "application/vnd.openxmlformats-officedocument.spreadsheetml.template.main+xml ")
	{
		throw xlnt::invalid_file(document_content_type);
	}
}

} // namespace

namespace xlnt {
namespace detail {

xlsx_consumer::xlsx_consumer(workbook &destination) : destination_(destination)
{
}

void xlsx_consumer::read(const path &source)
{
	destination_.clear();
	source_.load(source);
	populate_workbook();
}

void xlsx_consumer::read(std::istream &source)
{
	destination_.clear();
	source_.load(source);
	populate_workbook();
}

void xlsx_consumer::read(const std::vector<std::uint8_t> &source)
{
	destination_.clear();
	source_.load(source);
	populate_workbook();
}

// Part Writing Methods

void xlsx_consumer::populate_workbook()
{
	auto &manifest = destination_.get_manifest();
	read_manifest();

	for (const auto &rel : manifest.get_relationships(path("/")))
	{
        std::istringstream parser_stream(source_.read(rel.get_target().get_path()));
        xml::parser parser(parser_stream, rel.get_target().get_path().string());

		switch (rel.get_type())
		{
		case relationship::type::core_properties:
			read_core_properties(parser);
			break;
		case relationship::type::extended_properties:
			read_extended_properties(parser);
			break;
		case relationship::type::custom_properties:
			read_custom_property(parser);
			break;
		case relationship::type::office_document:
            check_document_type(manifest.get_content_type(rel.get_target().get_path()));
			read_workbook(parser);
			break;
		case relationship::type::connections:
			read_connections(parser);
			break;
		case relationship::type::custom_xml_mappings:
			read_custom_xml_mappings(parser);
			break;
		case relationship::type::external_workbook_references:
			read_external_workbook_references(parser);
			break;
		case relationship::type::metadata:
			read_metadata(parser);
			break;
		case relationship::type::pivot_table:
			read_pivot_table(parser);
			break;
		case relationship::type::shared_workbook_revision_headers:
			read_shared_workbook_revision_headers(parser);
			break;
		case relationship::type::volatile_dependencies:
			read_volatile_dependencies(parser);
			break;
        default:
            break;
		}
	}

    const auto workbook_rel = manifest.get_relationship(path("/"), relationship::type::office_document);

    // First pass of workbook relationship parts which must be read before sheets (e.g. shared strings)
    
	for (const auto &rel : manifest.get_relationships(workbook_rel.get_target().get_path()))
	{
		path part_path(rel.get_source().get_path().parent().append(rel.get_target().get_path()));
        std::istringstream parser_stream(source_.read(part_path));
        auto using_namespaces = rel.get_type() == relationship::type::styles;
        auto receive = xml::parser::receive_default
            | (using_namespaces ? xml::parser::receive_namespace_decls : 0);
        xml::parser parser(parser_stream, rel.get_target().get_path().string(), receive);

		switch (rel.get_type())
		{
        case relationship::type::shared_string_table:
            read_shared_string_table(parser);
            break;
        case relationship::type::styles:
            read_stylesheet(parser);
            break;
        case relationship::type::theme:
            read_theme(parser);
            break;
        default:
            break;
		}
	}
    
    // Second pass, read sheets themselves

	for (const auto &rel : manifest.get_relationships(workbook_rel.get_target().get_path()))
    {
		path part_path(rel.get_source().get_path().parent().append(rel.get_target().get_path()));
        std::istringstream parser_stream(source_.read(part_path));
        auto receive = xml::parser::receive_default | xml::parser::receive_namespace_decls;
        xml::parser parser(parser_stream, rel.get_target().get_path().string(), receive);

		switch (rel.get_type())
		{
		case relationship::type::chartsheet:
			read_chartsheet(rel.get_id(), parser);
			break;
		case relationship::type::dialogsheet:
			read_dialogsheet(rel.get_id(), parser);
			break;
		case relationship::type::worksheet:
			read_worksheet(rel.get_id(), parser);
			break;
        default:
            break;
		}
	}

	// Unknown Parts

	void read_unknown_parts();
	void read_unknown_relationships();
}

// Package Parts

void xlsx_consumer::read_manifest()
{
	path package_rels_path("_rels/.rels");
	if (!source_.has_file(package_rels_path)) throw invalid_file("missing package rels");
	auto package_rels = read_relationships(package_rels_path, source_);

    std::istringstream parser_stream(source_.read(path("[Content_Types].xml")));
    xml::parser parser(parser_stream, "[Content_Types].xml");
    
	auto &manifest = destination_.get_manifest();

    static const auto xmlns = constants::get_namespace("content-types");

    parser.next_expect(xml::parser::event_type::start_element, xmlns, "Types");
    parser.content(xml::content::complex);

    while (true)
	{
        if (parser.peek() == xml::parser::event_type::end_element) break;
        
        parser.next_expect(xml::parser::event_type::start_element);
        
        if (parser.name() == "Default")
        {
            manifest.register_default_type(parser.attribute("Extension"),
				parser.attribute("ContentType"));
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "Default");
        }
        else if (parser.name() == "Override")
        {
			manifest.register_override_type(path(parser.attribute("PartName")),
                parser.attribute("ContentType"));
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "Override");
        }
	}
    
    parser.next_expect(xml::parser::event_type::end_element, xmlns, "Types");

	for (const auto &package_rel : package_rels)
	{
		manifest.register_relationship(uri("/"),
            package_rel.get_type(),
			package_rel.get_target(),
			package_rel.get_target_mode(), 
			package_rel.get_id());
	}

	for (const auto &relationship_source : source_.infolist())
	{
		if (relationship_source.filename == path("_rels/.rels") 
			|| relationship_source.filename.extension() != "rels") continue;

		path part(relationship_source.filename.parent().parent());
		part = part.append(relationship_source.filename.split_extension().first);
		uri source(part.string());

		path source_directory = part.parent();

		auto part_rels = read_relationships(relationship_source.filename, source_);

		for (const auto part_rel : part_rels)
		{
			path target_path(source_directory.append(part_rel.get_target().get_path()));
			manifest.register_relationship(source, part_rel.get_type(),
				part_rel.get_target(), part_rel.get_target_mode(), part_rel.get_id());
		}
	}
}

void xlsx_consumer::read_extended_properties(xml::parser &parser)
{
    static const auto xmlns = constants::get_namespace("extended-properties");
    static const auto xmlns_vt = constants::get_namespace("vt");

    parser.next_expect(xml::parser::event_type::start_element, xmlns, "Properties");
    parser.content(xml::parser::content_type::complex);
    
    while (true)
	{
        if (parser.peek() == xml::parser::event_type::end_element) break;
        
        parser.next_expect(xml::parser::event_type::start_element);

        auto name = parser.name();
        auto text = std::string();

        while (parser.peek() == xml::parser::event_type::characters)
        {
            parser.next_expect(xml::parser::event_type::characters);
            text.append(parser.value());
        }
        
        if (name == "Application") destination_.set_application(text);
        else if (name == "DocSecurity") destination_.set_doc_security(std::stoi(text));
        else if (name == "ScaleCrop") destination_.set_scale_crop(is_true(text));
        else if (name == "Company") destination_.set_company(text);
        else if (name == "SharedDoc") destination_.set_shared_doc(is_true(text));
        else if (name == "HyperlinksChanged") destination_.set_hyperlinks_changed(is_true(text));
        else if (name == "AppVersion") destination_.set_app_version(text);
        else if (name == "Application") destination_.set_application(text);
        else if (name == "HeadingPairs")
        {
            parser.next_expect(xml::parser::event_type::start_element, xmlns_vt, "vector");
            parser.content(xml::parser::content_type::complex);

            parser.attribute("size");
            parser.attribute("baseType");

            parser.next_expect(xml::parser::event_type::start_element, xmlns_vt, "variant");
            parser.content(xml::parser::content_type::complex);
            parser.next_expect(xml::parser::event_type::start_element, xmlns_vt, "lpstr");
            parser.next_expect(xml::parser::event_type::characters);
            parser.next_expect(xml::parser::event_type::end_element, xmlns_vt, "lpstr");
            parser.next_expect(xml::parser::event_type::end_element, xmlns_vt, "variant");
            parser.next_expect(xml::parser::event_type::start_element, xmlns_vt, "variant");
            parser.content(xml::parser::content_type::complex);
            parser.next_expect(xml::parser::event_type::start_element, xmlns_vt, "i4");
            parser.next_expect(xml::parser::event_type::characters);
            parser.next_expect(xml::parser::event_type::end_element, xmlns_vt, "i4");
            parser.next_expect(xml::parser::event_type::end_element, xmlns_vt, "variant");
            
            parser.next_expect(xml::parser::event_type::end_element, xmlns_vt, "vector");
        }
        else if (name == "TitlesOfParts")
        {
            parser.next_expect(xml::parser::event_type::start_element, xmlns_vt, "vector");
            parser.content(xml::parser::content_type::complex);

            parser.attribute("size");
            parser.attribute("baseType");
            
            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;

                parser.next_expect(xml::parser::event_type::start_element, xmlns_vt, "lpstr");
                parser.content(xml::parser::content_type::simple);
                parser.next_expect(xml::parser::event_type::characters);
                parser.next_expect(xml::parser::event_type::end_element, xmlns_vt, "lpstr");
            }
            
            parser.next_expect(xml::parser::event_type::end_element, xmlns_vt, "vector");
        }
        
        while (parser.peek() == xml::parser::event_type::characters)
        {
            parser.next_expect(xml::parser::event_type::characters);
        }
        
        parser.next_expect(xml::parser::event_type::end_element);
	}
}

void xlsx_consumer::read_core_properties(xml::parser &parser)
{
    static const auto xmlns_cp = constants::get_namespace("core-properties");
    static const auto xmlns_dc = constants::get_namespace("dc");
    static const auto xmlns_dcterms = constants::get_namespace("dcterms");
    static const auto xmlns_dcmitype = constants::get_namespace("dcmitype");
    static const auto xmlns_xsi = constants::get_namespace("xsi");

    parser.next_expect(xml::parser::event_type::start_element, xmlns_cp, "coreProperties");
    parser.content(xml::parser::content_type::complex);
    
    while (true)
	{
        if (parser.peek() == xml::parser::event_type::end_element) break;
        
        parser.next_expect(xml::parser::event_type::start_element);
        parser.next_expect(xml::parser::event_type::characters);

		if (parser.namespace_() == xmlns_dc && parser.name() == "creator")
        {
            destination_.set_creator(parser.value());
        }
		else if (parser.namespace_() == xmlns_cp && parser.name() == "lastModifiedBy")
        {
            destination_.set_last_modified_by(parser.value());
        }
		else if (parser.namespace_() == xmlns_dcterms && parser.name() == "created")
        {
            parser.attribute(xml::qname(xmlns_xsi, "type"));
            destination_.set_created(w3cdtf_to_datetime(parser.value()));
        }
		else if (parser.namespace_() == xmlns_dcterms && parser.name() == "modified")
        {
            parser.attribute(xml::qname(xmlns_xsi, "type"));
            destination_.set_modified(w3cdtf_to_datetime(parser.value()));
        }
        
        parser.next_expect(xml::parser::event_type::end_element);
	}
    
    parser.next_expect(xml::parser::event_type::end_element, xmlns_cp, "coreProperties");
}

void xlsx_consumer::read_custom_file_properties(xml::parser &/*parser*/)
{
}

// Write SpreadsheetML-Specific Package Parts

void xlsx_consumer::read_workbook(xml::parser &parser)
{
    static const auto xmlns = constants::get_namespace("workbook");
    static const auto xmlns_mc = constants::get_namespace("mc");
    static const auto xmlns_mx = constants::get_namespace("mx");
    static const auto xmlns_r = constants::get_namespace("r");
    static const auto xmlns_s = constants::get_namespace("worksheet");
    static const auto xmlns_x15ac = constants::get_namespace("x15ac");

    parser.next_expect(xml::parser::event_type::start_element, xmlns, "workbook");
    parser.content(xml::parser::content_type::complex);
    
	while (parser.peek() == xml::parser::event_type::start_namespace_decl)
	{
        parser.next_expect(xml::parser::event_type::start_namespace_decl);
        if (parser.name() == "x15") destination_.enable_x15();
        parser.next_expect(xml::parser::event_type::end_namespace_decl);
	}
    
    if (parser.attribute_present(xml::qname(xmlns_mc, "Ignorable")))
    {
        parser.attribute(xml::qname(xmlns_mc, "Ignorable"));
    }

    while (true)
    {
        if (parser.peek() == xml::parser::event_type::end_element) break;
        
        parser.next_expect(xml::parser::event_type::start_element);
        parser.content(xml::parser::content_type::complex);

        auto qname = parser.qname();

        if (qname == xml::qname(xmlns, "fileVersion"))
        {
            destination_.d_->has_file_version_ = true;
            destination_.d_->file_version_.app_name = parser.attribute("appName");
            destination_.d_->file_version_.last_edited = string_to_size_t(parser.attribute("lastEdited"));
            destination_.d_->file_version_.lowest_edited = string_to_size_t(parser.attribute("lowestEdited"));
            destination_.d_->file_version_.rup_build = string_to_size_t(parser.attribute("rupBuild"));
            
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "fileVersion");
        }
        else if (qname == xml::qname(xmlns_mc, "AlternateContent"))
        {
            parser.next_expect(xml::parser::event_type::start_element, xmlns_mc, "Choice");
            parser.content(xml::parser::content_type::complex);
            parser.attribute("Requires");
            parser.next_expect(xml::parser::event_type::start_element, xmlns_x15ac, "absPath");
            destination_.set_absolute_path(path(parser.attribute("url")));
            parser.next_expect(xml::parser::event_type::end_element, xmlns_x15ac, "absPath");
            parser.next_expect(xml::parser::event_type::end_element, xmlns_mc, "Choice");
            parser.next_expect(xml::parser::event_type::end_element, xmlns_mc, "AlternateContent");
        }
        else if (qname == xml::qname(xmlns, "bookViews"))
        {
            if (parser.peek() == xml::parser::event_type::start_element)
            {
                parser.next_expect(xml::parser::event_type::start_element, xmlns, "workbookView");

                workbook_view view;
                view.x_window = string_to_size_t(parser.attribute("xWindow"));
                view.y_window = string_to_size_t(parser.attribute("yWindow"));
                view.window_width = string_to_size_t(parser.attribute("windowWidth"));
                view.window_height = string_to_size_t(parser.attribute("windowHeight"));
                view.tab_ratio = string_to_size_t(parser.attribute("tabRatio"));
                destination_.set_view(view);
                
                parser.next_expect(xml::parser::event_type::end_element, xmlns, "workbookView");
            }
            
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "bookViews");
        }
        else if (qname == xml::qname(xmlns, "workbookPr"))
        {
            destination_.d_->has_properties_ = true;

            if (parser.attribute_present("date1904"))
            {
                const auto value = parser.attribute("date1904");

                if (value == "1" || value == "true")
                {
                    destination_.set_base_date(xlnt::calendar::mac_1904);
                }
            }
            
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "workbookPr");
        }
        else if (qname == xml::qname(xmlns, "sheets"))
        {
            std::size_t index = 0;
            
            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                
                parser.next_expect(xml::parser::event_type::start_element, xmlns_s, "sheet");

                std::string rel_id(parser.attribute(xml::qname(xmlns_r, "id")));
                std::string title(parser.attribute("name"));
                auto id = string_to_size_t(parser.attribute("sheetId"));

                sheet_title_id_map_[title] = id;
                sheet_title_index_map_[title] = index++;
                destination_.d_->sheet_title_rel_id_map_[title] = rel_id;
                
                parser.next_expect(xml::parser::event_type::end_element, xmlns_s, "sheet");
            }
            
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "sheets");
        }
        else if (qname == xml::qname(xmlns, "calcPr"))
        {
            destination_.d_->has_calculation_properties_ = true;
            parser.attribute("calcId");
            parser.attribute("concurrentCalc");
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "calcPr");
        }
        else if (qname == xml::qname(xmlns, "extLst"))
        {
            parser.next_expect(xml::parser::event_type::start_element, xmlns, "ext");
            parser.content(xml::parser::content_type::complex);
            parser.attribute("uri");
            parser.next_expect(xml::parser::event_type::start_element, xmlns_mx, "ArchID");
            destination_.d_->has_arch_id_ = true;
            parser.attribute("Flags");
            parser.next_expect(xml::parser::event_type::end_element, xmlns_mx, "ArchID");
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "ext");
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "extLst");
        }
    }
    
    parser.next_expect(xml::parser::event_type::end_element, xmlns, "workbook");
}

// Write Workbook Relationship Target Parts

void xlsx_consumer::read_calculation_chain(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_chartsheet(const std::string &/*title*/, xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_connections(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_custom_property(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_custom_xml_mappings(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_dialogsheet(const std::string &/*title*/, xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_external_workbook_references(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_metadata(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_pivot_table(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_shared_string_table(xml::parser &parser)
{
    static const auto xmlns = constants::get_namespace("shared-strings");
    
    parser.next_expect(xml::parser::event_type::start_element, xmlns, "sst");
	std::size_t unique_count = 0;

	if (parser.attribute_present("uniqueCount"))
	{
		unique_count = string_to_size_t(parser.attribute("uniqueCount"));
	}

	auto &strings = destination_.get_shared_strings();

    while (true)
    {
        if (parser.peek() == xml::parser::event_type::end_element) break;
        
        parser.next_expect(xml::parser::event_type::start_element, xmlns, "si");
        parser.next_expect(xml::parser::event_type::start_element);
        
        text t;
        
		if (parser.name() == "t")
		{
			t.set_plain_string(parser.value());
		}
		else if (parser.name() == "r") // possible multiple text entities.
		{
            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                
                parser.next_expect(xml::parser::event_type::start_element, xmlns, "t");
                
                text_run run;
                run.set_string(parser.value());

                if (parser.peek() == xml::parser::event_type::start_element)
                {
                    parser.next_expect(xml::parser::event_type::start_element, xmlns, "rPr");

                    while (true)
                    {
                        if (parser.peek() == xml::parser::event_type::end_element) break;
                        
                        parser.next_expect(xml::parser::event_type::start_element);

                        if (parser.qname() == xml::qname(xmlns, "sz"))
                        {
                            run.set_size(string_to_size_t(parser.attribute("val")));
                        }
                        else if (parser.qname() == xml::qname(xmlns, "rFont"))
                        {
                            run.set_font(parser.attribute("val"));
                        }
                        else if (parser.qname() == xml::qname(xmlns, "color"))
                        {
                            run.set_color(parser.attribute("rgb"));
                        }
                        else if (parser.qname() == xml::qname(xmlns, "family"))
                        {
                            run.set_family(string_to_size_t(parser.attribute("val")));
                        }
                        else if (parser.qname() == xml::qname(xmlns, "scheme"))
                        {
                            run.set_scheme(parser.attribute("val"));
                        }
                        
                        parser.next_expect(xml::parser::event_type::end_element, parser.qname());
                    }
                }

                t.add_run(run);
            }
		}
        
        strings.push_back(t);
	}

	if (unique_count != strings.size())
	{
		throw invalid_file("sizes don't match");
	}
}

void xlsx_consumer::read_shared_workbook_revision_headers(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_shared_workbook(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_shared_workbook_user_data(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_stylesheet(xml::parser &parser)
{
    static const auto xmlns = constants::get_namespace("worksheet");
    static const auto xmlns_mc = constants::get_namespace("mc");
    static const auto xmlns_x14 = constants::get_namespace("x14");
    static const auto xmlns_x14ac = constants::get_namespace("x14ac");
    
	auto &stylesheet = destination_.impl().stylesheet_;

    parser.next_expect(xml::parser::event_type::start_element, xmlns, "styleSheet");
    parser.content(xml::parser::content_type::complex);

    while (true)
    {
        if (parser.peek() != xml::parser::event_type::start_namespace_decl) break;

        parser.next_expect(xml::parser::event_type::start_namespace_decl);

        if (parser.namespace_() == xmlns_x14ac)
        {
            destination_.enable_x15();
        }
    }
    
    if (parser.attribute_present(xml::qname(xmlns_mc, "Ignorable")))
    {
        parser.attribute(xml::qname(xmlns_mc, "Ignorable"));
    }
    
    struct formatting_record
    {
        std::pair<class alignment, bool> alignment = { {}, 0 };
        std::pair<std::size_t, bool> border_id = { 0, false };
        std::pair<std::size_t, bool> fill_id = { 0, false };
        std::pair<std::size_t, bool> font_id = { 0, false };
        std::pair<std::size_t, bool> number_format_id = { 0, false };
        std::pair<class protection, bool> protection = { {}, false };
        std::pair<std::size_t, bool> style_id = { 0, false };
    };
    
    struct style_data
    {
        std::string name;
        std::size_t record_id;
        std::size_t builtin_id;
    };

    std::vector<style_data> style_datas;
    std::vector<formatting_record> style_records;
    std::vector<formatting_record> format_records;

    while (true)
    {
        if (parser.peek() == xml::parser::event_type::end_element) break;

        parser.next_expect(xml::parser::event_type::start_element);
        parser.content(xml::parser::content_type::complex);

        if (parser.qname() == xml::qname(xmlns, "borders"))
        {
            stylesheet.borders.clear();
            
            auto count = parser.attribute<std::size_t>("count");
    
            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                stylesheet.borders.push_back(read_border(parser));
            }
                        
            if (count != stylesheet.borders.size())
            {
                throw xlnt::exception("counts don't match");
            }
        }
        else if (parser.qname() == xml::qname(xmlns, "fills"))
        {
            stylesheet.fills.clear();
            
            auto count = parser.attribute<std::size_t>("count");

            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                stylesheet.fills.push_back(read_fill(parser));
            }
                        
            if (count != stylesheet.fills.size())
            {
                throw xlnt::exception("counts don't match");
            }
        }
        else if (parser.qname() == xml::qname(xmlns, "fonts"))
        {
            stylesheet.fonts.clear();

            auto count = parser.attribute<std::size_t>("count");
            
            if (parser.attribute_present(xml::qname(xmlns_x14ac, "knownFonts")))
            {
                parser.attribute(xml::qname(xmlns_x14ac, "knownFonts"));
            }

            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                stylesheet.fonts.push_back(read_font(parser));
            }
            
            if (count != stylesheet.fonts.size())
            {
                throw xlnt::exception("counts don't match");
            }
        }
        else if (parser.qname() == xml::qname(xmlns, "numFmts"))
        {
            stylesheet.number_formats.clear();

            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                
                parser.next_expect(xml::parser::event_type::start_element, "numFmt");
                auto format_string = parser.attribute("formatCode");

                if (format_string == "GENERAL")
                {
                    format_string = "General";
                }

                xlnt::number_format nf;

                nf.set_format_string(format_string);
                nf.set_id(string_to_size_t(parser.attribute("numFmtId")));

                stylesheet.number_formats.push_back(nf);
            }
        }
        else if (parser.qname() == xml::qname(xmlns, "colors"))
        {
        }
        else if (parser.qname() == xml::qname(xmlns, "cellStyles"))
        {
            auto count = parser.attribute<std::size_t>("count");
            
            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                
                auto &data = *style_datas.emplace(style_datas.end());
                
                parser.next_expect(xml::parser::event_type::start_element, xmlns, "cellStyle");
                data.name = parser.attribute("name");
                data.record_id = parser.attribute<std::size_t>("xfId");
                data.builtin_id = parser.attribute<std::size_t>("builtinId");
                parser.next_expect(xml::parser::event_type::end_element, xmlns, "cellStyle");
            }
            
            if (count != style_datas.size())
            {
                throw xlnt::exception("counts don't match");
            }
        }
        else if (parser.qname() == xml::qname(xmlns, "cellStyleXfs")
            || parser.qname() == xml::qname(xmlns, "cellXfs"))
        {
            auto in_style_records = parser.name() == "cellStyleXfs";
            auto count = parser.attribute<std::size_t>("count");
            
            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                
                parser.next_expect(xml::parser::event_type::start_element, xmlns, "xf");

                auto &record = *(!in_style_records
                    ? format_records.emplace(format_records.end())
                    : style_records.emplace(style_records.end()));

                auto apply_alignment_present = parser.attribute_present("applyAlignment");
                auto alignment_applied = apply_alignment_present
                    && is_true(parser.attribute("applyAlignment"));
                record.alignment.second = alignment_applied;

                auto border_applied = parser.attribute_present("applyBorder")
                    && is_true(parser.attribute("applyBorder"));
                auto border_index = parser.attribute_present("borderId")
                    ? string_to_size_t(parser.attribute("borderId")) : 0;
                record.border_id = { border_index, border_applied };
                
                auto fill_applied = parser.attribute_present("applyFill")
                    && is_true(parser.attribute("applyFill"));
                auto fill_index = parser.attribute_present("fillId")
                    ? string_to_size_t(parser.attribute("fillId")) : 0;
                record.fill_id = { fill_index, fill_applied };

                auto font_applied = parser.attribute_present("applyFont")
                    && is_true(parser.attribute("applyFont"));
                auto font_index = parser.attribute_present("fontId")
                    ? string_to_size_t(parser.attribute("fontId")) : 0;
                record.font_id = { font_index, font_applied };

                auto number_format_applied = parser.attribute_present("applyNumberFormat")
                    && is_true(parser.attribute("applyNumberFormat"));
                auto number_format_id = parser.attribute_present("numFmtId")
                    ? string_to_size_t(parser.attribute("numFmtId")) : 0;
                record.number_format_id = { number_format_id, number_format_applied };

                auto apply_protection_present = parser.attribute_present("applyProtection");
                auto protection_applied = apply_protection_present
                    && is_true(parser.attribute("applyProtection"));
                record.protection.second = protection_applied;

                if (parser.attribute_present("xfId") && parser.name() == "cellXfs")
                {
                    record.style_id = { parser.attribute<std::size_t>("xfId"), true };
                }
                
                while (true)
                {
                    if (parser.peek() == xml::parser::event_type::end_element) break;
                    
                    parser.next_expect(xml::parser::event_type::start_element);
                    
                    if (parser.qname() == xml::qname(xmlns, "alignment"))
                    {
                        record.alignment.first = read_alignment(parser);
                        record.alignment.second = !apply_alignment_present || alignment_applied;
                    }
                    else if (parser.qname() == xml::qname(xmlns, "protection"))
                    {
                        record.protection.first = read_protection(parser);
                        record.protection.second = !apply_protection_present || protection_applied;
                    }
                    
                    parser.next_expect(xml::parser::event_type::end_element, parser.qname());
                }
                
                parser.next_expect(xml::parser::event_type::end_element, xmlns, "xf");
                
            }
            
            if ((in_style_records && count != style_records.size())
                || (!in_style_records && count != format_records.size()))
            {
                throw xlnt::exception("counts don't match");
            }
        }
        else if (parser.qname() == xml::qname(xmlns, "dxfs"))
        {
            auto count = parser.attribute<std::size_t>("count");
            std::size_t processed = 0;
            
            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                parser.next_expect(xml::parser::event_type::start_element);
                parser.next_expect(xml::parser::event_type::end_element);
            }
            
            if (count != processed)
            {
                throw xlnt::exception("counts don't match");
            }
        }
        else if (parser.qname() == xml::qname(xmlns, "tableStyles"))
        {
            auto default_table_style = parser.attribute("defaultTableStyle");
            auto default_pivot_style = parser.attribute("defaultPivotStyle");
            auto count = parser.attribute<std::size_t>("count");
            std::size_t processed = 0;
            
            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                parser.next_expect(xml::parser::event_type::start_element);
                parser.next_expect(xml::parser::event_type::end_element);
            }
            
            if (count != processed)
            {
                throw xlnt::exception("counts don't match");
            }
        }
        else if (parser.qname() == xml::qname(xmlns, "extLst"))
        {
            parser.next_expect(xml::parser::event_type::start_element, xmlns, "ext");
            parser.content(xml::parser::content_type::complex);
            parser.attribute("uri");
            parser.next_expect(xml::parser::event_type::start_namespace_decl);
            parser.next_expect(xml::parser::event_type::start_element, xmlns_x14, "slicerStyles");
            parser.attribute("defaultSlicerStyle");
            parser.next_expect(xml::parser::event_type::end_element, xmlns_x14, "slicerStyles");
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "ext");
            parser.next_expect(xml::parser::event_type::end_namespace_decl);
        }
        
        parser.next_expect(xml::parser::event_type::end_element);
    }
    
    parser.next_expect(xml::parser::event_type::end_element, xmlns, "styleSheet");
    
    auto lookup_number_format = [&](std::size_t number_format_id)
    {
        auto result = number_format::general();
        bool is_custom_number_format = false;
        
        for (const auto &nf : stylesheet.number_formats)
        {
            if (nf.get_id() == number_format_id)
            {
                result = nf;
                is_custom_number_format = true;
                break;
            }
        }

        if (number_format_id < 164 && !is_custom_number_format)
        {
            result = number_format::from_builtin_id(number_format_id);
        }
        
        return result;
    };
    
    auto style_data_iter = style_datas.begin();
    
    for (const auto &record : style_records)
    {
        auto &new_style = stylesheet.create_style();

        new_style.name(style_data_iter->name);
        new_style.builtin_id(style_data_iter->builtin_id);

        new_style.alignment(record.alignment.first, record.alignment.second);
        new_style.border(stylesheet.borders.at(record.border_id.first), record.border_id.second);
        new_style.fill(stylesheet.fills.at(record.fill_id.first), record.fill_id.second);
        new_style.font(stylesheet.fonts.at(record.font_id.first), record.font_id.second);
        new_style.number_format(lookup_number_format(record.number_format_id.first), record.number_format_id.second);
        new_style.protection(record.protection.first, record.protection.second);

        ++style_data_iter;
    }
    
    for (const auto &record : format_records)
    {
        auto &new_format = stylesheet.create_format();
        
        new_format.style(stylesheet.styles.at(record.style_id.first).name());

        new_format.alignment(record.alignment.first, record.alignment.second);
        new_format.border(stylesheet.borders.at(record.border_id.first), record.border_id.second);
        new_format.fill(stylesheet.fills.at(record.fill_id.first), record.fill_id.second);
        new_format.font(stylesheet.fonts.at(record.font_id.first), record.font_id.second);
        new_format.number_format(lookup_number_format(record.number_format_id.first), record.number_format_id.second);
        new_format.protection(record.protection.first, record.protection.second);
    }
}

void xlsx_consumer::read_theme(xml::parser &/*parser*/)
{
	destination_.set_theme(theme());
}

void xlsx_consumer::read_volatile_dependencies(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_worksheet(const std::string &rel_id, xml::parser &parser)
{
    static const auto xmlns = constants::get_namespace("worksheet");
    static const auto xmlns_mc = constants::get_namespace("mc");
    static const auto xmlns_x14ac = constants::get_namespace("x14ac");

	auto title = std::find_if(destination_.d_->sheet_title_rel_id_map_.begin(),
		destination_.d_->sheet_title_rel_id_map_.end(),
		[&](const std::pair<std::string, std::string> &p)
	{
		return p.second == rel_id;
	})->first;

	auto id = sheet_title_id_map_[title];
	auto index = sheet_title_index_map_[title];

	auto insertion_iter = destination_.d_->worksheets_.begin();
	while (insertion_iter != destination_.d_->worksheets_.end()
		&& sheet_title_index_map_[insertion_iter->title_] < index)
	{
		++insertion_iter;
	}

	destination_.d_->worksheets_.emplace(insertion_iter, &destination_, id, title);

	auto ws = destination_.get_sheet_by_id(id);

	parser.next_expect(xml::parser::event_type::start_element, xmlns, "worksheet");
    parser.content(xml::parser::content_type::complex);

    while (parser.peek() == xml::parser::event_type::start_namespace_decl)
	{
        parser.next_expect(xml::parser::event_type::start_namespace_decl);

        if (parser.namespace_() == xmlns_x14ac)
        {
            ws.enable_x14ac();
        }
	}
    
    if (parser.attribute_present(xml::qname(xmlns_mc, "Ignorable")))
    {
        parser.attribute(xml::qname(xmlns_mc, "Ignorable"));
    }

	xlnt::range_reference full_range;

    while (true)
    {
        if (parser.peek() == xml::parser::event_type::end_element) break;
        
        parser.next_expect(xml::parser::event_type::start_element);
        parser.content(xml::parser::content_type::complex);
        
        if (parser.qname() == xml::qname(xmlns, "dimension"))
        {
            full_range = xlnt::range_reference(parser.attribute("ref"));
            ws.d_->has_dimension_ = true;
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "dimension");
        }
        else if (parser.qname() == xml::qname(xmlns, "sheetViews"))
        {
            ws.d_->has_view_ = true;
            
            while (true)
            {
                parser.attribute_map();

                if (parser.next() == xml::parser::event_type::end_element && parser.name() == "sheetViews")
                {
                    break;
                }
            }
            
            //parser.next_expect(xml::parser::event_type::end_element, xmlns, "sheetViews");
        }
        else if (parser.qname() == xml::qname(xmlns, "sheetFormatPr"))
        {
            ws.d_->has_format_properties_ = true;
            
            while (true)
            {
                parser.attribute_map();

                if (parser.next() == xml::parser::event_type::end_element && parser.name() == "sheetFormatPr")
                {
                    break;
                }
            }
            
            //parser.next_expect(xml::parser::event_type::end_element, xmlns, "sheetFormatPr");
        }
        else if (parser.qname() == xml::qname(xmlns, "mergeCells"))
        {
            auto count = std::stoull(parser.attribute("count"));

            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;

                parser.next_expect(xml::parser::event_type::start_element, xmlns, "mergeCell");
                ws.merge_cells(range_reference(parser.attribute("ref")));
                parser.next_expect(xml::parser::event_type::start_element, xmlns, "mergeCell");

                count--;
            }

            if (count != 0)
            {
                throw invalid_file("sizes don't match");
            }
            
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "mergeCells");
        }
        else if (parser.qname() == xml::qname(xmlns, "sheetData"))
        {
            auto &shared_strings = destination_.get_shared_strings();

            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                
                parser.next_expect(xml::parser::event_type::start_element, xmlns, "row");

                auto row_index = static_cast<row_t>(std::stoull(parser.attribute("r")));

                if (parser.attribute_present("ht"))
                {
                    ws.get_row_properties(row_index).height = std::stold(parser.attribute("ht"));
                }

                std::string span_string = parser.attribute("spans");
                auto colon_index = span_string.find(':');

                column_t min_column = 0;
                column_t max_column = 0;

                if (colon_index != std::string::npos)
                {
                    min_column = static_cast<column_t::index_t>(std::stoll(span_string.substr(0, colon_index)));
                    max_column = static_cast<column_t::index_t>(std::stoll(span_string.substr(colon_index + 1)));
                }
                else
                {
                    min_column = full_range.get_top_left().get_column_index();
                    max_column = full_range.get_bottom_right().get_column_index();
                }

                while (true)
                {
                    if (parser.peek() == xml::parser::event_type::end_element) break;

                    parser.next_expect(xml::parser::event_type::start_element, xmlns, "c");
                    auto cell = ws.get_cell(cell_reference(parser.attribute("r")));
                    
                    auto has_type = parser.attribute_present("t");
                    auto type = has_type ? parser.attribute("t") : "";

                    auto has_format = parser.attribute_present("s");
                    auto format_id = static_cast<std::size_t>(has_format ? std::stoull(parser.attribute("s")) : 0LL);
    
                    auto has_value = false;
                    auto value_string = std::string();
                    
                    auto has_formula = false;
                    auto has_shared_formula = false;
                    auto formula_value_string = std::string();
    
                    while (true)
                    {
                        if (parser.peek() == xml::parser::event_type::end_element) break;
                        
                        parser.next_expect(xml::parser::event_type::start_element);
                        
                        if (parser.qname() == xml::qname(xmlns, "v"))
                        {
                            has_value = true;
                            value_string = parser.value();
                        }
                        else if (parser.qname() == xml::qname(xmlns, "f"))
                        {
                            has_formula = true;
                            has_shared_formula = parser.attribute_present("t") && parser.attribute("t") == "shared";
                            formula_value_string = parser.value();
                        }
                        else if (parser.qname() == xml::qname(xmlns, "is"))
                        {
                            parser.next_expect(xml::parser::event_type::start_element, xmlns, "t");
                            value_string = parser.value();
                            parser.next_expect(xml::parser::event_type::end_element, xmlns, "t");
                        }
                        
                        parser.next_expect(xml::parser::event_type::end_element, parser.qname());
                    }

                    if (has_formula && !has_shared_formula && !ws.get_workbook().get_data_only())
                    {
                        cell.set_formula(formula_value_string);
                    }

                    if (has_type && (type == "inlineStr" || type =="str"))
                    {
                        cell.set_value(value_string);
                    }
                    else if (has_type && type == "s" && !has_formula)
                    {
                        auto shared_string_index = static_cast<std::size_t>(std::stoull(value_string));
                        auto shared_string = shared_strings.at(shared_string_index);
                        cell.set_value(shared_string);
                    }
                    else if (has_type && type == "b") // boolean
                    {
                        cell.set_value(value_string != "0");
                    }
                    else if (has_value && !value_string.empty())
                    {
                        if (!value_string.empty() && value_string[0] == '#')
                        {
                            cell.set_error(value_string);
                        }
                        else
                        {
                            cell.set_value(std::stold(value_string));
                        }
                    }

                    if (has_format)
                    {
                        cell.set_format(destination_.get_format(format_id));
                    }
                    
                    parser.next_expect(xml::parser::event_type::end_element, xmlns, "c");
                }
            }
            
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "sheetData");
        }
        else if (parser.qname() == xml::qname(xmlns, "cols"))
        {
            while (true)
            {
                if (parser.peek() == xml::parser::event_type::end_element) break;
                
                parser.next_expect(xml::parser::event_type::start_element, xmlns, "col");

                auto min = static_cast<column_t::index_t>(std::stoull(parser.attribute("min")));
                auto max = static_cast<column_t::index_t>(std::stoull(parser.attribute("max")));
                auto width = std::stold(parser.attribute("width"));
                bool custom = parser.attribute("customWidth") == std::string("1");
                auto column_style = static_cast<std::size_t>(parser.attribute_present("style") ? std::stoull(parser.attribute("style")) : 0);

                for (auto column = min; column <= max; column++)
                {
                    if (!ws.has_column_properties(column))
                    {
                        ws.add_column_properties(column, column_properties());
                    }

                    ws.get_column_properties(min).width = width;
                    ws.get_column_properties(min).style = column_style;
                    ws.get_column_properties(min).custom = custom;
                }
                
                parser.next_expect(xml::parser::event_type::end_element, xmlns, "col");
            }
            
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "cols");
        }
        else if (parser.qname() == xml::qname(xmlns, "autoFilter"))
        {
            ws.auto_filter(xlnt::range_reference(parser.attribute("ref")));
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "autoFilter");
        }
        else if (parser.qname() == xml::qname(xmlns, "pageMargins"))
        {
            page_margins margins;

            margins.set_top(parser.attribute<double>("top"));
            margins.set_bottom(parser.attribute<double>("bottom"));
            margins.set_left(parser.attribute<double>("left"));
            margins.set_right(parser.attribute<double>("right"));
            margins.set_header(parser.attribute<double>("header"));
            margins.set_footer(parser.attribute<double>("footer"));

            ws.set_page_margins(margins);
            
            parser.next_expect(xml::parser::event_type::end_element, xmlns, "pageMargins");
        }
    }
    
    parser.next_expect(xml::parser::event_type::end_element, xmlns, "worksheet");
}

// Sheet Relationship Target Parts

void xlsx_consumer::read_comments(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_drawings(xml::parser &/*parser*/)
{
}

// Unknown Parts

void xlsx_consumer::read_unknown_parts(xml::parser &/*parser*/)
{
}

void xlsx_consumer::read_unknown_relationships(xml::parser &/*parser*/)
{
}

} // namespace detail
} // namepsace xlnt
