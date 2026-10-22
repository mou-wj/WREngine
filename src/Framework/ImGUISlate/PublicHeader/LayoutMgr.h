#pragma once
#include "Widget.h"

#include <vector>
#include <cstdint>





namespace ImGUISlate{

//----------------------------------------
// Layout Params
//----------------------------------------

struct LayoutMargin
{
    float Left = 0;
    float Right = 0;
    float Top = 0;
    float Bottom = 0;
};


struct LayoutParams
{
    float Width = -1;
    float Height = -1;

    float Weight = 0;

    LayoutMargin Margin;

    uint32_t Row = 0;
    uint32_t Column = 0;

    uint32_t RowSpan = 1;
    uint32_t ColumnSpan = 1;
};


//----------------------------------------
// Layout Element
//----------------------------------------

struct LayoutElement
{
    SlateCore::Widget* WidgetPtr = nullptr;

    LayoutParams Params;
};


//----------------------------------------
// Layout Manager
//----------------------------------------

class IMGUISLATE_API LayoutManager
{
public:

    virtual ~LayoutManager() = default;


public:

    void AddWidget(SlateCore::Widget* widget, const LayoutParams& params = {});

    void RemoveWidget(SlateCore::Widget* widget);

    virtual void Layout(float width, float height) = 0;


protected:

    std::vector<LayoutElement> _elements;
};



//----------------------------------------
// Vertical Layout
//----------------------------------------

class IMGUISLATE_API VerticalLayout : public LayoutManager
{
public:

    void Layout(float width, float height) override;
};



//----------------------------------------
// Horizontal Layout
//----------------------------------------

class IMGUISLATE_API HorizontalLayout : public LayoutManager
{
public:

    void Layout(float width, float height) override;
};



//----------------------------------------
// Grid Layout
//----------------------------------------

class IMGUISLATE_API GridLayout : public LayoutManager
{
public:

    GridLayout(uint32_t rows, uint32_t columns);

    void Layout(float width, float height) override;


private:

    uint32_t m_Rows = 1;

    uint32_t m_Columns = 1;
};
} // namespace ImGUISlate