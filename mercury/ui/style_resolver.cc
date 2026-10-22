#include "style_resolver.h"

#include <algorithm>
#include <functional>
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
      if (expanded.tokens.size() == 1 && expanded.tokens[0].IsIdent("inherit")) {
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

std::vector<MatchedRule> MatchRules(const VisualElement* element,
                                    const std::vector<const StyleSheet*>& sheets) {
  std::vector<MatchedRule> matches;
  for (size_t s = 0; s < sheets.size(); s++) {
    const StyleSheet* sheet = sheets[s];
    for (size_t r = 0; r < sheet->rules.size(); r++) {
      const StyleRule& rule = sheet->rules[r];
      const ComplexSelector* best = nullptr;
      for (const auto& selector : rule.selectors) {
        if (selector.Matches(*element) &&
            (!best || selector.specificity > best->specificity)) {
          best = &selector;
        }
      }
      if (best) {
        matches.push_back({sheet, &rule, best, best->specificity,
                           static_cast<int>(s), static_cast<int>(r)});
      }
    }
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

void ResolveStyles(VisualElement* root, UIResources& resources,
                   const std::vector<const StyleSheet*>& panel_sheets,
                   bool animate) {
  static const ComputedStyle kDefault;
  static const std::unordered_map<std::string, TokenList> kNoVariables;

  std::function<void(VisualElement*, bool)> visit = [&](VisualElement* e,
                                                        bool forced) {
    const bool need = forced || e->styleDirty();
    if (!need && !e->subtreeStyleDirty()) return;
    if (need) {
      const VisualElement* parent = e->hierarchyParent();
      const ComputedStyle& parent_style = parent ? parent->computedStyle() : kDefault;
      ComputedStyle computed;
      computed.InheritFrom(parent_style);

      const auto sheets = ApplicableSheets(e, resources, panel_sheets);
      const auto matches = MatchRules(e, sheets);

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
    e->ClearStyleDirty();
    // A restyled element restyles its whole subtree: descendant selectors
    // and inherited values may have changed.
    for (const auto& child : e->hierarchyChildren()) visit(child.get(), need);
  };
  visit(root, false);
}

}  // namespace Mercury::UI
