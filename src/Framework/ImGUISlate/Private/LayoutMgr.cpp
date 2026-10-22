
#include "LayoutMgr.h"
#include "Widget.h"

#include <algorithm>

namespace ImGUISlate{

void LayoutManager::AddWidget(
    SlateCore::Widget* widget,
    const LayoutParams& params)
{
    _elements.push_back(
        {
            widget,
            params
        });
}


inline void LayoutManager::RemoveWidget(SlateCore::Widget* widget)
{
    _elements.erase(
        std::remove_if(
            _elements.begin(),
            _elements.end(),
            [&](auto& e)
            {
                return e.WidgetPtr == widget;
            }),
        _elements.end());
}



//----------------------------------------
// Vertical Layout
//----------------------------------------

inline void VerticalLayout::Layout(float width, float height)
{
    float fixedHeight = 0;
    float totalWeight = 0;


    for (auto& e : _elements)
    {
        if (e.Params.Weight > 0)
        {
            totalWeight += e.Params.Weight;
        }
        else
        {
            fixedHeight += e.Params.Height;
        }
    }


    float remainHeight = height - fixedHeight;

    float y = 0;


    for (auto& e : _elements)
    {
        float h;

        if (e.Params.Weight > 0)
        {
            h = remainHeight *
                e.Params.Weight /
                totalWeight;
        }
        else
        {
            h = e.Params.Height;
        }


        auto& m = e.Params.Margin;


        e.WidgetPtr->SetGeometry(
            m.Left,
            y + m.Top,
            width - m.Left - m.Right,
            h - m.Top - m.Bottom);


        y += h;
    }
}



//----------------------------------------
// Horizontal Layout
//----------------------------------------

inline void HorizontalLayout::Layout(float width, float height)
{
    float fixedWidth = 0;
    float totalWeight = 0;


    for (auto& e : _elements)
    {
        if (e.Params.Weight > 0)
        {
            totalWeight += e.Params.Weight;
        }
        else
        {
            fixedWidth += e.Params.Width;
        }
    }


    float remainWidth = width - fixedWidth;

    float x = 0;


    for (auto& e : _elements)
    {
        float w;


        if (e.Params.Weight > 0)
        {
            w = remainWidth *
                e.Params.Weight /
                totalWeight;
        }
        else
        {
            w = e.Params.Width;
        }


        auto& m = e.Params.Margin;


        e.WidgetPtr->SetGeometry(
            x + m.Left,
            m.Top,
            w - m.Left - m.Right,
            height - m.Top - m.Bottom);


        x += w;
    }
}



//----------------------------------------
// Grid Layout
//----------------------------------------

inline GridLayout::GridLayout(uint32_t rows, uint32_t columns)
    :
    m_Rows(rows),
    m_Columns(columns)
{
}



inline void GridLayout::Layout(float width, float height)
{
    float cellWidth = width / m_Columns;
    float cellHeight = height / m_Rows;


    for (auto& e : _elements)
    {
        auto& p = e.Params;
        auto& m = p.Margin;


        float x = p.Column * cellWidth;
        float y = p.Row * cellHeight;


        float w = cellWidth * p.ColumnSpan;
        float h = cellHeight * p.RowSpan;


        e.WidgetPtr->SetGeometry(
            x + m.Left,
            y + m.Top,
            w - m.Left - m.Right,
            h - m.Top - m.Bottom);
    }
}
}