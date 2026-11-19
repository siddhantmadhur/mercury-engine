// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "style_resolver.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "ui/resources.h"
#include "ui/visual_element.h"

namespace Mercury::UI {

namespace {

bool MatchSimple(const SimpleSelector& part, const VisualElement& e) {
  if (!part.type.empty() && part.type != "*" && part.type != e.typeName()) {
    return false;
  }
  if (!part.name.empty() && part.name != e.name()) return false;
  for (const auto& cls : part.classes) {
    if (!e.ClassListContains(cls)) return false;
  }
  const uint32_t states = e.pseudoStates();
  if ((states & part.pseudo) != part.pseudo) return false;
  if (states & part.not_pseudo) return false;
  return true;
}

bool MatchFrom(const ComplexSelector& s, int i, const VisualElement* e,
               bool ignore_state) {
  if (ignore_state && i == static_cast<int>(s.parts.size()) - 1) {
    SimpleSelector stateless = s.parts[i];
    stateless.pseudo &= kPseudoRoot;
    stateless.not_pseudo = 0;
    if (!MatchSimple(stateless, *e)) return false;
  } else if (!MatchSimple(s.parts[i], *e)) {
    return false;
  }
  if (i == 0) return true;
  const auto combinator = s.combinators[i - 1];
  const VisualElement* parent = e->hierarchyParent();
  if (combinator == ComplexSelector::Combinator::kChild) {
    return parent && MatchFrom(s, i - 1, parent, false);
  }
  for (const VisualElement* p = parent; p; p = p->hierarchyParent()) {
    if (MatchFrom(s, i - 1, p, false)) return true;
  }
  return false;
}

void ApplyDeclarations(
    const StyleSheet* sheet, const std::vector<StyleDeclaration>& declarations,
    const std::unordered_map<std::string, TokenList>& variables,
    ComputedStyle& computed, const ComputedStyle& parent) {
  for (const auto& d : declarations) {
    if (d.property.starts_with("--")) continue;
    TokenList tokens;
    if (!SubstituteVariables(d.tokens, variables, tokens)) continue;
    for (const auto& expanded : ExpandDeclaration(d.property, tokens)) {
      if (expanded.tokens.size() == 1 &&
          expanded.tokens[0].IsIdent("inherit")) {
        computed.Set(expanded.id, parent.Get(expanded.id));
        continue;
      }
      if (auto value = ParseStyleValue(expanded.id, expanded.tokens,
                                       sheet ? sheet->path : "")) {
        computed.Set(expanded.id, *value);
      }
    }
  }
}

constexpr size_t kMaxRuleIndexCache = 16;

struct RuleRef {
  const StyleSheet* sheet;
  const StyleRule* rule;
  int sheet_order;
  int rule_order;
};

// Rules bucketed by the element type their rightmost selector requires, so
// matching an element only scans rules that can possibly apply to it. Rules
// with a universal rightmost selector (or no selectors) go in `universal`.
struct RuleIndex {
  std::map<std::string, std::vector<RuleRef>, std::less<>> by_type;
  std::vector<RuleRef> universal;
};

void BuildRuleIndex(const std::vector<const StyleSheet*>& sheets,
                    RuleIndex& index) {
  for (size_t s = 0; s < sheets.size(); s++) {
    const StyleSheet* sheet = sheets[s];
    if (!sheet) continue;
    for (size_t r = 0; r < sheet->rules.size(); r++) {
      const StyleRule& rule = sheet->rules[r];
      const RuleRef ref{sheet, &rule, static_cast<int>(s), static_cast<int>(r)};
      bool universal = false;
      std::vector<std::string_view> types;
      for (const auto& selector : rule.selectors) {
        if (selector.parts.empty()) {
          universal = true;
          break;
        }
        const std::string& type = selector.parts.back().type;
        if (type.empty() || type == "*") {
          universal = true;
          break;
        }
        if (std::find(types.begin(), types.end(), type) == types.end()) {
          types.push_back(type);
        }
      }
      if (universal || types.empty()) {
        index.universal.push_back(ref);
        continue;
      }
      for (const auto& type : types)
        index.by_type[std::string(type)].push_back(ref);
    }
  }
}

void MatchOneRule(const RuleRef& ref, const VisualElement& element,
                  std::vector<MatchedRule>& matches) {
  const ComplexSelector* best = nullptr;
  for (const auto& selector : ref.rule->selectors) {
    if (selector.Matches(element) &&
        (!best || selector.specificity > best->specificity)) {
      best = &selector;
    }
  }
  if (best) {
    matches.push_back({ref.sheet, ref.rule, best, best->specificity,
                       ref.sheet_order, ref.rule_order});
  }
}

std::vector<MatchedRule> MatchIndexedRules(const VisualElement* element,
                                           const RuleIndex& index) {
  std::vector<MatchedRule> matches;
  for (const auto& ref : index.universal) MatchOneRule(ref, *element, matches);
  if (auto it = index.by_type.find(element->typeName());
      it != index.by_type.end()) {
    for (const auto& ref : it->second) MatchOneRule(ref, *element, matches);
  }
  std::stable_sort(matches.begin(), matches.end(),
                   [](const MatchedRule& a, const MatchedRule& b) {
                     if (a.specificity != b.specificity) {
                       return a.specificity < b.specificity;
                     }
                     if (a.sheet_order != b.sheet_order) {
                       return a.sheet_order < b.sheet_order;
                     }
                     return a.rule_order < b.rule_order;
                   });
  return matches;
}

}  // namespace

bool ComplexSelector::Matches(const VisualElement& element) const {
  return !parts.empty() &&
         MatchFrom(*this, static_cast<int>(parts.size()) - 1, &element, false);
}

bool ComplexSelector::MatchesIgnoringState(const VisualElement& element) const {
  return !parts.empty() &&
         MatchFrom(*this, static_cast<int>(parts.size()) - 1, &element, true);
}

std::vector<const StyleSheet*> ApplicableSheets(
    const VisualElement* element, UIResources& resources,
    const std::vector<const StyleSheet*>& panel_sheets) {
  std::vector<const StyleSheet*> sheets = panel_sheets;
  std::vector<const VisualElement*> chain;
  for (const VisualElement* e = element; e; e = e->hierarchyParent()) {
    chain.push_back(e);
  }
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    for (const auto& sheet : (*it)->styleSheets()) {
      resources.CollectSheets(sheet, sheets);
    }
  }
  return sheets;
}

std::vector<MatchedRule> MatchRules(
    const VisualElement* element,
    const std::vector<const StyleSheet*>& sheets) {
  RuleIndex index;
  BuildRuleIndex(sheets, index);
  return MatchIndexedRules(element, index);
}

void ResolveStyles(VisualElement* root, UIResources& resources,
                   const std::vector<const StyleSheet*>& panel_sheets,
                   bool animate) {
  static const ComputedStyle kDefault;
  static const std::unordered_map<std::string, TokenList> kNoVariables;

  // Sheet sets repeat across the tree (usually just the panel's sheets), so
  // build one rule index per distinct set and reuse it for the whole pass.
  // The cache is per call and bounded, so edited sheets never go stale.
  std::map<std::vector<const StyleSheet*>, RuleIndex> indices;
  auto index_for =
      [&](const std::vector<const StyleSheet*>& sheets) -> const RuleIndex& {
    auto it = indices.find(sheets);
    if (it == indices.end()) {
      if (indices.size() >= kMaxRuleIndexCache) indices.clear();
      it = indices.emplace(sheets, RuleIndex{}).first;
      BuildRuleIndex(sheets, it->second);
    }
    return it->second;
  };

  // Iterative pre-order walk: CustomStyleResolvedEvent handlers may remove
  // elements, so a recursive walk could both overflow the stack on deep trees
  // and invalidate the iteration. Each entry carries the element's alive
  // token and is checked again before use.
  struct Pending {
    VisualElement* element;
    std::shared_ptr<const bool> alive;
    bool forced;
  };
  std::vector<Pending> stack;
  stack.push_back({root, root->aliveToken(), false});
  while (!stack.empty()) {
    const Pending entry = std::move(stack.back());
    stack.pop_back();
    if (!*entry.alive) continue;
    VisualElement* e = entry.element;

    const bool need = entry.forced || e->styleDirty();
    if (!need && !e->subtreeStyleDirty()) continue;
    if (need) {
      const VisualElement* parent = e->hierarchyParent();
      const ComputedStyle& parent_style =
          parent ? parent->computedStyle() : kDefault;
      ComputedStyle computed;
      computed.InheritFrom(parent_style);

      const auto sheets = ApplicableSheets(e, resources, panel_sheets);
      const auto matches = MatchIndexedRules(e, index_for(sheets));

      // Custom properties inherit, then the matched rules add theirs.
      auto variables = parent ? parent->customStyle() : kNoVariables;
      bool has_own = false;
      for (const auto& m : matches) {
        for (const auto& d : m.rule->declarations) {
          if (!d.property.starts_with("--")) continue;
          TokenList tokens;
          if (SubstituteVariables(d.tokens, variables, tokens)) {
            variables[d.property] = std::move(tokens);
            has_own = true;
          }
        }
      }
      for (const auto& m : matches) {
        ApplyDeclarations(m.sheet, m.rule->declarations, variables, computed,
                          parent_style);
      }
      std::as_const(*e).style().ApplyTo(computed);
      e->ClearStyleDirty();
      e->SetComputedStyle(computed, animate);
      e->SetCustomProperties(std::move(variables));
      if (has_own) {
        CustomStyleResolvedEvent event;
        e->SendEvent(event);
      }
    }
    // A handler above may have destroyed the element and its children.
    if (!*entry.alive) continue;
    e->ClearStyleDirty();
    // A restyled element restyles its whole subtree: descendant selectors
    // and inherited values may have changed. Push a snapshot of the children,
    // reversed so they are visited in tree order.
    const auto& children = e->hierarchyChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      VisualElement* child = it->get();
      stack.push_back({child, child->aliveToken(), need});
    }
  }
}

}  // namespace Mercury::UI
