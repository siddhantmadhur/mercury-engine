/*
 * style_resolver.h
 *
 * The USS cascade: which rules match an element, in what order, and the
 * resulting ComputedStyle. Rules are ordered by selector specificity, then
 * by sheet order (theme, then sheets attached higher in the tree, then
 * lower), then by position in the sheet; inline styles win over all of them.
 */
#ifndef MERCURY_UI_STYLE_RESOLVER_H
#define MERCURY_UI_STYLE_RESOLVER_H

#include <vector>

#include "ui/style_sheet.h"

namespace Mercury::UI {

class VisualElement;
class UIResources;

struct MatchedRule {
  const StyleSheet* sheet;
  const StyleRule* rule;
  const ComplexSelector* selector;  // the most specific matching one
  int specificity;
  int sheet_order;
  int rule_order;
};

// Every sheet that applies to `element`, lowest priority first.
std::vector<const StyleSheet*> ApplicableSheets(
    const VisualElement* element, UIResources& resources,
    const std::vector<const StyleSheet*>& panel_sheets);

// Matching rules, lowest priority first.
std::vector<MatchedRule> MatchRules(const VisualElement* element,
                                    const std::vector<const StyleSheet*>& sheets);

/**
 * @brief Restyles every element marked dirty under `root`
 * @param animate start transitions for changed values
 */
void ResolveStyles(VisualElement* root, UIResources& resources,
                   const std::vector<const StyleSheet*>& panel_sheets,
                   bool animate);

}  // namespace Mercury::UI

#endif  // MERCURY_UI_STYLE_RESOLVER_H
