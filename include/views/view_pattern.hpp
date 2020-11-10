#pragma once

#include "parser/ast_node.hpp"
#include "parser/parser.hpp"
#include "parser/lexer.hpp"
#include "views/view.hpp"

#include <concepts>
#include <cstring>

#include "views/view_hexeditor.hpp"

namespace hex {

    class ViewPattern : public View {
    public:
        ViewPattern(ViewHexEditor *hexEditor) : View(), m_hexEditor(hexEditor) {
            this->m_buffer = new char[0xFFFFFF];
            std::memset(this->m_buffer, 0x00, 0xFFFFFF);
        }
        virtual ~ViewPattern() {
            delete[] this->m_buffer;
        }

        virtual void createMenu() {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Load pattern...")) {
                    auto filePath = openFileDialog();
                    if (filePath.has_value()) {
                        FILE *file = fopen(filePath->c_str(), "r+b");

                        fseek(file, 0, SEEK_END);
                        size_t size = ftell(file);
                        rewind(file);

                        if (size > 0xFF'FFFF)
                            return;

                        fread(this->m_buffer, size, 1, file);

                        fclose(file);

                        this->parsePattern(this->m_buffer);
                    }

                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Pattern View", "", &this->m_windowOpen);
                ImGui::EndMenu();
            }
        }

        void createView() override {
            if (!this->m_windowOpen)
                return;

            ImGui::Begin("Pattern", &this->m_windowOpen, ImGuiWindowFlags_None);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

            auto size = ImGui::GetWindowSize();
            size.y -= 50;
            ImGui::InputTextMultiline("Pattern", this->m_buffer, 0xFFFF, size, ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackEdit,
                [](ImGuiInputTextCallbackData* data) -> int {
                    auto _this = static_cast<ViewPattern*>(data->UserData);

                    _this->parsePattern(data->Buf);

                    return 0;
                }, this
            );
            ImGui::PopStyleVar(2);
            ImGui::End();
        }

    private:
        char *m_buffer;

        ViewHexEditor *m_hexEditor;
        bool m_windowOpen = true;

        void parsePattern(char *buffer) {
            static hex::lang::Lexer lexer;
            static hex::lang::Parser parser;

            this->m_hexEditor->clearHighlights();

            auto [lexResult, tokens] = lexer.lex(buffer);

            if (lexResult.failed()) {
                return;
            }

            auto [parseResult, ast] = parser.parse(tokens);
            if (parseResult.failed()) {
                for(auto &node : ast) delete node;
                return;
            }

            for (auto &varNode : this->findNodes<lang::ASTNodeVariableDecl>(lang::ASTNode::Type::VariableDecl, ast)) {
                if (!varNode->getOffset().has_value())
                    continue;

                u64 offset = varNode->getOffset().value();
                if (varNode->getVariableType() != lang::Token::TypeToken::Type::CustomType) {
                    this->m_hexEditor->setHighlight(offset, static_cast<u32>(varNode->getVariableType()) >> 4);
                } else {
                    for (auto &structNode : this->findNodes<lang::ASTNodeStruct>(lang::ASTNode::Type::Struct, ast))
                        if (varNode->getCustomVariableTypeName() == structNode->getName())
                            if (this->highlightStruct(ast, structNode, offset) == -1)
                                this->m_hexEditor->clearHighlights();

                    for (auto &usingNode : this->findNodes<lang::ASTNodeTypeDecl>(lang::ASTNode::Type::TypeDecl, ast))
                        if (varNode->getCustomVariableTypeName() == usingNode->getTypeName())
                            if (this->highlightUsingDecls(ast, usingNode, offset) == -1)
                                this->m_hexEditor->clearHighlights();
                }

            }

            for(auto &node : ast) delete node;
        }

        template<std::derived_from<lang::ASTNode> T>
        std::vector<T*> findNodes(const lang::ASTNode::Type type, const std::vector<lang::ASTNode*> &nodes) const noexcept {
            std::vector<T*> result;

            for (const auto & node : nodes)
                if (node->getType() == type)
                    result.push_back(static_cast<T*>(node));

            return result;
        }

        s32 highlightUsingDecls(std::vector<lang::ASTNode*> &ast, lang::ASTNodeTypeDecl* currTypeDeclNode, u64 offset) {
            if (currTypeDeclNode->getAssignedType() != lang::Token::TypeToken::Type::CustomType) {
                size_t size = static_cast<u32>(currTypeDeclNode->getAssignedType()) >> 4;

                this->m_hexEditor->setHighlight(offset, size);
                offset += size;
            } else {
                bool foundType = false;
                for (auto &structNode : findNodes<lang::ASTNodeStruct>(lang::ASTNode::Type::Struct, ast))
                    if (structNode->getName() == currTypeDeclNode->getAssignedCustomTypeName()) {
                        offset = this->highlightStruct(ast, structNode, offset);
                        foundType = true;
                        break;
                    }

                for (auto &typeDeclNode : findNodes<lang::ASTNodeTypeDecl>(lang::ASTNode::Type::TypeDecl, ast))
                    if (typeDeclNode->getTypeName() == currTypeDeclNode->getAssignedCustomTypeName()) {
                        offset = this->highlightUsingDecls(ast, typeDeclNode, offset);
                        foundType = true;
                        break;
                    }

                if (!foundType)
                    return -1;
            }

            return offset;
        }

        s32 highlightStruct(std::vector<lang::ASTNode*> &ast, lang::ASTNodeStruct* currStructNode, u64 offset) {
            u64 startOffset = offset;

            for (auto &node : currStructNode->getNodes()) {
                auto var = static_cast<lang::ASTNodeVariableDecl*>(node);

                if (var->getVariableType() != lang::Token::TypeToken::Type::CustomType) {
                    size_t size = static_cast<u32>(var->getVariableType()) >> 4;

                    this->m_hexEditor->setHighlight(offset, size);
                    offset += size;
                } else {
                    bool foundType = false;
                    for (auto &structNode : findNodes<lang::ASTNodeStruct>(lang::ASTNode::Type::Struct, ast))
                        if (structNode->getName() == var->getCustomVariableTypeName()) {
                            auto size = this->highlightStruct(ast, structNode, offset);

                            if (size == -1)
                                return -1;

                            offset += size;
                            foundType = true;
                            break;
                        }

                    for (auto &typeDeclNode : findNodes<lang::ASTNodeTypeDecl>(lang::ASTNode::Type::TypeDecl, ast))
                        if (typeDeclNode->getTypeName() == var->getCustomVariableTypeName()) {
                            auto size = this->highlightUsingDecls(ast, typeDeclNode, offset);

                            if (size == -1)
                                return -1;

                            offset = size;
                            foundType = true;
                            break;
                        }

                    if (!foundType)
                        return -1;
                }

            }

            return offset - startOffset;
        }
    };

}