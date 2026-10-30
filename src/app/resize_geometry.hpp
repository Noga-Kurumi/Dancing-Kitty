#pragma once
#include <algorithm>
#include <cmath>
namespace catpulse {
enum class Corner {None,TopLeft,TopRight,BottomLeft,BottomRight};
struct Square {int x=0,y=0,size=260;};
struct Bounds {int left=0,top=0,right=0,bottom=0;};
struct NormalizedBounds {double left=0,top=0,right=1,bottom=1;};
inline Bounds scaledBounds(NormalizedBounds b,int side){
    return {int(std::floor(b.left*side)),int(std::floor(b.top*side)),int(std::ceil(b.right*side)),int(std::ceil(b.bottom*side))};
}
inline Corner cornerAtBounds(Bounds b,int x,int y){
    if(x<b.left||y<b.top||x>=b.right||y>=b.bottom)return Corner::None;
    int grip=std::clamp(std::min(b.right-b.left,b.bottom-b.top)/8,8,24);
    bool left=x<b.left+grip,right=x>=b.right-grip,top=y<b.top+grip,bottom=y>=b.bottom-grip;
    if(left&&top)return Corner::TopLeft;if(right&&top)return Corner::TopRight;
    if(left&&bottom)return Corner::BottomLeft;if(right&&bottom)return Corner::BottomRight;return Corner::None;
}
inline Square resizedVisible(Square before,NormalizedBounds normalized,Corner corner,int dx,int dy){
    if(corner==Corner::None)return before;
    auto bounds=scaledBounds(normalized,before.size);
    bool left=corner==Corner::TopLeft||corner==Corner::BottomLeft,top=corner==Corner::TopLeft||corner==Corner::TopRight;
    double xScale=double((left?-dx:dx))/std::max(1,bounds.right-bounds.left),yScale=double((top?-dy:dy))/std::max(1,bounds.bottom-bounds.top);
    int side=std::clamp(int(std::lround(before.size*(1+(std::abs(xScale)>=std::abs(yScale)?xScale:yScale)))),96,1000);
    auto after=scaledBounds(normalized,side);
    return {before.x+(left?bounds.right-after.right:bounds.left-after.left),before.y+(top?bounds.bottom-after.bottom:bounds.top-after.top),side};
}
}
