#include "html_parser.h"

#include <cctype>
#include <stack>

#ifdef XSQL_USE_LIBXML2
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#endif

namespace xsql {

namespace {

#ifndef XSQL_USE_LIBXML2
bool is_name_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ':';
}

void skip_ws(const std::string& s, size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
}
#endif

std::string to_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

}  // namespace

#ifdef XSQL_USE_LIBXML2

namespace {

void append_text_to_stack(HtmlDocument& doc, const std::vector<int64_t>& stack, const char* text) {
  if (!text) return;
  std::string value(text);
  if (value.empty()) return;
  for (int64_t node_id : stack) {
    doc.nodes[static_cast<size_t>(node_id)].text += value;
  }
}

std::string dump_inner_html(xmlNode* node) {
  if (!node || !node->children) return "";
  xmlBufferPtr buffer = xmlBufferCreate();
  if (!buffer) return "";
  for (xmlNode* child = node->children; child != nullptr; child = child->next) {
    xmlNodeDump(buffer, node->doc, child, 0, 0);
  }
  std::string out;
  const xmlChar* content = xmlBufferContent(buffer);
  if (content) {
    out = reinterpret_cast<const char*>(content);
  }
  xmlBufferFree(buffer);
  return out;
}

void walk_node(HtmlDocument& doc, xmlNode* node, std::vector<int64_t>& stack) {
  for (xmlNode* cur = node; cur != nullptr; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      HtmlNode out;
      out.id = static_cast<int64_t>(doc.nodes.size());
      out.tag = to_lower(reinterpret_cast<const char*>(cur->name));
      if (!stack.empty()) {
        out.parent_id = stack.back();
      }
      for (xmlAttr* attr = cur->properties; attr != nullptr; attr = attr->next) {
        std::string name = to_lower(reinterpret_cast<const char*>(attr->name));
        xmlChar* value = xmlNodeListGetString(cur->doc, attr->children, 1);
        if (value) {
          out.attributes[name] = reinterpret_cast<const char*>(value);
          xmlFree(value);
        } else {
          out.attributes[name] = "";
        }
      }
      out.inner_html = dump_inner_html(cur);
      doc.nodes.push_back(out);
      stack.push_back(out.id);
      if (cur->children) {
        walk_node(doc, cur->children, stack);
      }
      stack.pop_back();
    } else if (cur->type == XML_TEXT_NODE || cur->type == XML_CDATA_SECTION_NODE) {
      append_text_to_stack(doc, stack, reinterpret_cast<const char*>(cur->content));
    } else if (cur->children) {
      walk_node(doc, cur->children, stack);
    }
  }
}

}  // namespace

HtmlDocument parse_html(const std::string& html) {
  HtmlDocument doc;
  htmlDocPtr html_doc = htmlReadMemory(
      html.data(),
      static_cast<int>(html.size()),
      nullptr,
      nullptr,
      HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
  if (!html_doc) {
    return doc;
  }

  xmlNode* root = xmlDocGetRootElement(html_doc);
  std::vector<int64_t> stack;
  if (root) {
    walk_node(doc, root, stack);
  }
  xmlFreeDoc(html_doc);
  return doc;
}

#else

HtmlDocument parse_html(const std::string& html) {
  HtmlDocument doc;
  struct OpenNode {
    int64_t id = 0;
    size_t content_start = 0;
  };
  std::string lower_html = to_lower(html);
  std::vector<OpenNode> stack;
  size_t i = 0;
  while (i < html.size()) {
    if (html[i] == '<') {
      if (html.compare(i, 4, "<!--") == 0) {
        size_t end = html.find("-->", i + 4);
        i = (end == std::string::npos) ? html.size() : end + 3;
        continue;
      }
      if (i + 1 < html.size() && html[i + 1] == '/') {
        size_t close_start = i;
        i += 2;
        while (i < html.size() && is_name_char(html[i])) {
          ++i;
        }
        size_t close = html.find('>', i);
        i = (close == std::string::npos) ? html.size() : close + 1;
        if (!stack.empty()) {
          OpenNode open = stack.back();
          if (close_start >= open.content_start) {
            doc.nodes[static_cast<size_t>(open.id)].inner_html =
                html.substr(open.content_start, close_start - open.content_start);
          }
          stack.pop_back();
        }
        continue;
      }

      ++i;
      skip_ws(html, i);
      std::string tag;
      while (i < html.size() && is_name_char(html[i])) {
        tag.push_back(html[i++]);
      }
      if (tag.empty()) {
        ++i;
        continue;
      }
      HtmlNode node;
      node.id = static_cast<int64_t>(doc.nodes.size());
      node.tag = to_lower(tag);
      if (!stack.empty()) {
        node.parent_id = stack.back().id;
      }

      bool self_close = false;
      while (i < html.size()) {
        skip_ws(html, i);
        if (i >= html.size()) break;
        if (html[i] == '/') {
          self_close = true;
          ++i;
          skip_ws(html, i);
          if (i < html.size() && html[i] == '>') {
            ++i;
          }
          break;
        }
        if (html[i] == '>') {
          ++i;
          break;
        }

        std::string attr_name;
        while (i < html.size() && is_name_char(html[i])) {
          attr_name.push_back(html[i++]);
        }
        if (attr_name.empty()) {
          ++i;
          continue;
        }
        attr_name = to_lower(attr_name);
        skip_ws(html, i);
        std::string attr_value;
        if (i < html.size() && html[i] == '=') {
          ++i;
          skip_ws(html, i);
          if (i < html.size() && (html[i] == '\'' || html[i] == '"')) {
            char quote = html[i++];
            while (i < html.size() && html[i] != quote) {
              attr_value.push_back(html[i++]);
            }
            if (i < html.size() && html[i] == quote) ++i;
          } else {
            while (i < html.size() && !std::isspace(static_cast<unsigned char>(html[i])) && html[i] != '>' && html[i] != '/') {
              attr_value.push_back(html[i++]);
            }
          }
        }
        if (!attr_name.empty()) {
          node.attributes[attr_name] = attr_value;
        }
      }

      doc.nodes.push_back(node);
      HtmlNode& current = doc.nodes.back();
      size_t content_start = i;
      if (!self_close && (node.tag == "script" || node.tag == "style")) {
        std::string close_tag = "</" + node.tag;
        size_t close_start = lower_html.find(close_tag, content_start);
        if (close_start == std::string::npos) {
          current.inner_html = html.substr(content_start);
          current.text += current.inner_html;
          i = html.size();
          continue;
        }
        current.inner_html = html.substr(content_start, close_start - content_start);
        current.text += current.inner_html;
        size_t close_end = html.find('>', close_start);
        i = (close_end == std::string::npos) ? html.size() : close_end + 1;
        continue;
      }
      if (!self_close) {
        stack.push_back(OpenNode{node.id, content_start});
      }
      continue;
    }

    size_t start = i;
    while (i < html.size() && html[i] != '<') {
      ++i;
    }
    if (!stack.empty()) {
      std::string text = html.substr(start, i - start);
      for (const auto& open : stack) {
        doc.nodes[static_cast<size_t>(open.id)].text += text;
      }
    }
  }

  return doc;
}

#endif

}  // namespace xsql
