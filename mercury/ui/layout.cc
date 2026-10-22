#include "layout.h"

#include <yoga/Yoga.h>

#include <cmath>

#include "ui/visual_element.h"

namespace Mercury::UI {

namespace {

YGSize MeasureCallback(YGNodeConstRef node, float width, YGMeasureMode width_mode,
                       float height, YGMeasureMode height_mode) {
  auto* element = static_cast<VisualElement*>(YGNodeGetContext(node));
  auto mode = [](YGMeasureMode m) {
    switch (m) {
      case YGMeasureModeExactly: return MeasureMode::kExactly;
      case YGMeasureModeAtMost: return MeasureMode::kAtMost;
      default: return MeasureMode::kUndefined;
    }
  };
  glm::vec2 size = element->Measure(width, mode(width_mode), height, mode(height_mode));
  return {size.x, size.y};
}

YGAlign ToYoga(Align a) {
  switch (a) {
    case Align::kAuto: return YGAlignAuto;
    case Align::kFlexStart: return YGAlignFlexStart;
    case Align::kCenter: return YGAlignCenter;
    case Align::kFlexEnd: return YGAlignFlexEnd;
    case Align::kStretch: return YGAlignStretch;
    case Align::kSpaceBetween: return YGAlignSpaceBetween;
    case Align::kSpaceAround: return YGAlignSpaceAround;
  }
  return YGAlignAuto;
}

YGJustify ToYoga(Justify j) {
  switch (j) {
    case Justify::kFlexStart: return YGJustifyFlexStart;
    case Justify::kCenter: return YGJustifyCenter;
    case Justify::kFlexEnd: return YGJustifyFlexEnd;
    case Justify::kSpaceBetween: return YGJustifySpaceBetween;
    case Justify::kSpaceAround: return YGJustifySpaceAround;
    case Justify::kSpaceEvenly: return YGJustifySpaceEvenly;
  }
  return YGJustifyFlexStart;
}

YGFlexDirection ToYoga(FlexDirection d) {
  switch (d) {
    case FlexDirection::kColumn: return YGFlexDirectionColumn;
    case FlexDirection::kColumnReverse: return YGFlexDirectionColumnReverse;
    case FlexDirection::kRow: return YGFlexDirectionRow;
    case FlexDirection::kRowReverse: return YGFlexDirectionRowReverse;
  }
  return YGFlexDirectionColumn;
}

YGWrap ToYoga(Wrap w) {
  switch (w) {
    case Wrap::kNoWrap: return YGWrapNoWrap;
    case Wrap::kWrap: return YGWrapWrap;
    case Wrap::kWrapReverse: return YGWrapWrapReverse;
  }
  return YGWrapNoWrap;
}

template <typename SetPx, typename SetPercent, typename SetAuto>
void SetLength(const Length& l, SetPx px, SetPercent percent, SetAuto automatic) {
  switch (l.unit) {
    case Length::Unit::kPixel: px(l.value); break;
    case Length::Unit::kPercent: percent(l.value); break;
    case Length::Unit::kAuto:
    case Length::Unit::kNone: automatic(); break;
  }
}

void SetEdge(YGNodeRef n, YGEdge edge, const Length& l,
             void (*px)(YGNodeRef, YGEdge, float),
             void (*percent)(YGNodeRef, YGEdge, float),
             void (*automatic)(YGNodeRef, YGEdge)) {
  switch (l.unit) {
    case Length::Unit::kPixel: px(n, edge, l.value); break;
    case Length::Unit::kPercent: percent(n, edge, l.value); break;
    case Length::Unit::kAuto:
    case Length::Unit::kNone:
      if (automatic) {
        automatic(n, edge);
      } else {
        px(n, edge, YGUndefined);
      }
      break;
  }
}

void ReadLayout(VisualElement* element, std::vector<GeometryChange>& changes) {
  YGNodeRef node = element->yogaNode();
  const Rect rect{YGNodeLayoutGetLeft(node), YGNodeLayoutGetTop(node),
                  YGNodeLayoutGetWidth(node), YGNodeLayoutGetHeight(node)};
  if (!(rect == element->layout())) {
    changes.push_back({element, element->layout(), rect});
    element->SetLayout(rect);
  }
  for (const auto& child : element->hierarchyChildren()) {
    ReadLayout(child.get(), changes);
  }
}

}  // namespace

YGConfig* SharedYogaConfig() {
  static YGConfigRef config = [] {
    YGConfigRef c = YGConfigNew();
    // Unity rounds to whole points; panels apply their scale afterwards.
    YGConfigSetPointScaleFactor(c, 1.0f);
    return c;
  }();
  return config;
}

void VisualElement::ApplyStyleToYoga() {
  YGNodeRef n = yoga_node_;
  const ComputedStyle& s = resolved_;

  SetLength(s.width, [&](float v) { YGNodeStyleSetWidth(n, v); },
            [&](float v) { YGNodeStyleSetWidthPercent(n, v); },
            [&] { YGNodeStyleSetWidthAuto(n); });
  SetLength(s.height, [&](float v) { YGNodeStyleSetHeight(n, v); },
            [&](float v) { YGNodeStyleSetHeightPercent(n, v); },
            [&] { YGNodeStyleSetHeightAuto(n); });
  SetLength(s.minWidth, [&](float v) { YGNodeStyleSetMinWidth(n, v); },
            [&](float v) { YGNodeStyleSetMinWidthPercent(n, v); },
            [&] { YGNodeStyleSetMinWidth(n, YGUndefined); });
  SetLength(s.minHeight, [&](float v) { YGNodeStyleSetMinHeight(n, v); },
            [&](float v) { YGNodeStyleSetMinHeightPercent(n, v); },
            [&] { YGNodeStyleSetMinHeight(n, YGUndefined); });
  SetLength(s.maxWidth, [&](float v) { YGNodeStyleSetMaxWidth(n, v); },
            [&](float v) { YGNodeStyleSetMaxWidthPercent(n, v); },
            [&] { YGNodeStyleSetMaxWidth(n, YGUndefined); });
  SetLength(s.maxHeight, [&](float v) { YGNodeStyleSetMaxHeight(n, v); },
            [&](float v) { YGNodeStyleSetMaxHeightPercent(n, v); },
            [&] { YGNodeStyleSetMaxHeight(n, YGUndefined); });

  YGNodeStyleSetFlexDirection(n, ToYoga(s.flexDirection));
  YGNodeStyleSetFlexWrap(n, ToYoga(s.flexWrap));
  YGNodeStyleSetFlexGrow(n, s.flexGrow);
  YGNodeStyleSetFlexShrink(n, s.flexShrink);
  SetLength(s.flexBasis, [&](float v) { YGNodeStyleSetFlexBasis(n, v); },
            [&](float v) { YGNodeStyleSetFlexBasisPercent(n, v); },
            [&] { YGNodeStyleSetFlexBasisAuto(n); });
  YGNodeStyleSetAlignItems(n, ToYoga(s.alignItems));
  YGNodeStyleSetAlignContent(n, ToYoga(s.alignContent));
  YGNodeStyleSetAlignSelf(n, ToYoga(s.alignSelf));
  YGNodeStyleSetJustifyContent(n, ToYoga(s.justifyContent));
  YGNodeStyleSetPositionType(n, s.position == Position::kAbsolute
                                    ? YGPositionTypeAbsolute
                                    : YGPositionTypeRelative);
  YGNodeStyleSetDisplay(n, s.display == DisplayStyle::kNone ? YGDisplayNone
                                                            : YGDisplayFlex);
  YGNodeStyleSetOverflow(n, s.overflow == Overflow::kHidden ? YGOverflowHidden
                                                            : YGOverflowVisible);

  SetEdge(n, YGEdgeLeft, s.left, YGNodeStyleSetPosition,
          YGNodeStyleSetPositionPercent, YGNodeStyleSetPositionAuto);
  SetEdge(n, YGEdgeTop, s.top, YGNodeStyleSetPosition,
          YGNodeStyleSetPositionPercent, YGNodeStyleSetPositionAuto);
  SetEdge(n, YGEdgeRight, s.right, YGNodeStyleSetPosition,
          YGNodeStyleSetPositionPercent, YGNodeStyleSetPositionAuto);
  SetEdge(n, YGEdgeBottom, s.bottom, YGNodeStyleSetPosition,
          YGNodeStyleSetPositionPercent, YGNodeStyleSetPositionAuto);

  SetEdge(n, YGEdgeLeft, s.marginLeft, YGNodeStyleSetMargin,
          YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
  SetEdge(n, YGEdgeTop, s.marginTop, YGNodeStyleSetMargin,
          YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
  SetEdge(n, YGEdgeRight, s.marginRight, YGNodeStyleSetMargin,
          YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
  SetEdge(n, YGEdgeBottom, s.marginBottom, YGNodeStyleSetMargin,
          YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);

  SetEdge(n, YGEdgeLeft, s.paddingLeft, YGNodeStyleSetPadding,
          YGNodeStyleSetPaddingPercent, nullptr);
  SetEdge(n, YGEdgeTop, s.paddingTop, YGNodeStyleSetPadding,
          YGNodeStyleSetPaddingPercent, nullptr);
  SetEdge(n, YGEdgeRight, s.paddingRight, YGNodeStyleSetPadding,
          YGNodeStyleSetPaddingPercent, nullptr);
  SetEdge(n, YGEdgeBottom, s.paddingBottom, YGNodeStyleSetPadding,
          YGNodeStyleSetPaddingPercent, nullptr);

  YGNodeStyleSetBorder(n, YGEdgeLeft, s.borderLeftWidth);
  YGNodeStyleSetBorder(n, YGEdgeTop, s.borderTopWidth);
  YGNodeStyleSetBorder(n, YGEdgeRight, s.borderRightWidth);
  YGNodeStyleSetBorder(n, YGEdgeBottom, s.borderBottomWidth);
}

void VisualElement::UpdateMeasureFunc() {
  const bool want = HasMeasure() && children_.empty();
  if (want == has_measure_func_) return;
  YGNodeSetMeasureFunc(yoga_node_, want ? MeasureCallback : nullptr);
  has_measure_func_ = want;
}

bool IsLayoutDirty(VisualElement* root) { return YGNodeIsDirty(root->yogaNode()); }

void CalculateLayout(VisualElement* root, float width, float height,
                     std::vector<GeometryChange>& changes) {
  YGNodeRef node = root->yogaNode();
  YGNodeStyleSetWidth(node, width);
  YGNodeStyleSetHeight(node, height);
  YGNodeCalculateLayout(node, width, height, YGDirectionLTR);
  ReadLayout(root, changes);
}

}  // namespace Mercury::UI
