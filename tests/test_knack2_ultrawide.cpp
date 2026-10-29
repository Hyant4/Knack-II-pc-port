// SPDX-License-Identifier: GPL-2.0-or-later
#include "tlg_graphics.h"
#include <cstdio>
#include <limits>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main() {
    namespace G=TlgGraphics;
    CHECK(G::KnackProfile("CUSA08014-01.01") && G::KnackProfile("CUSA07670-01.01"));
    CHECK(G::KnackProfile("CUSA08014-01.00"));
    CHECK(!G::KnackProfile("CUSA07670-01.00") && !G::KnackProfile("CUSA08014-01.02"));
    CHECK(!G::KnackProfile(nullptr) && !G::KnackProfile("CUSA07670-01.00"));
    CHECK(G::RequestedAspect(0,1920,1080)==G::NativeAspect);
    CHECK(G::RequestedAspect(0,1080,1920)==G::NativeAspect);
    CHECK(G::RequestedAspect(0,0,0)==G::NativeAspect);
    CHECK(G::RequestedAspect(1,5120,1440)==G::NativeAspect);
    const G::KnackVideoImage video{7,0x1000,42,2560,1056};
    CHECK(video.Matches(7,0x1000,42,2560,1056));
    CHECK(video.Matches(8,0x1000,42,2560,1056)); // alias copy of the same unique write
    CHECK(!video.Matches(7,0x1000,43,2560,1056)); // another write invalidates the association
    CHECK(!video.Matches(7,0x2000,42,2560,1056));
    CHECK(!video.Matches(7,0x1000,42,1920,1080));
    CHECK(!G::KnackVideoImage{}.Matches(0,0,0,0,0));
    using Pass=G::KnackVideoPass;
    G::KnackVideoFraming framing;
    CHECK(framing.Advance(false,false)); // publisher/logo/loading before the first scene
    CHECK(framing.Advance(true,false));  // movie
    CHECK(framing.Advance(false,false)); // skip: movie is gone, transition is still blank
    CHECK(framing.Advance(false,false)); // loading remains framed
    CHECK(!framing.Advance(false,true)); // actual gameplay camera, release ultrawide
    CHECK(!framing.Advance(false,false)); // pause or a held gameplay frame stays wide
    CHECK(framing.Advance(true,true));   // video over a live scene takes precedence
    CHECK(framing.Advance(false,false));
    CHECK(!framing.Advance(false,true));
    const auto draw=[](uint64_t fragment, bool sampled=false, bool indirect=false,
                       bool depth=false, bool clip=false, unsigned indices=4,
                       unsigned instances=1, bool knack=true, uint64_t vertex=0xa4d9b6a0) {
        return G::KnackVideoDraw(knack,vertex,fragment,depth,clip,indirect,indices,instances,sampled);
    };
    CHECK(draw(0x6e46c324)==Pass::Conversion);
    CHECK(draw(0x6e46c324,false,true)==Pass::Conversion);
    CHECK(draw(0xc437421c,true)==Pass::Composition);
    CHECK(draw(0xc437421c)==Pass::None); // ordinary sprites and text retain their background
    CHECK(draw(0x6e46c324,false,false,true)==Pass::None);
    CHECK(draw(0x6e46c324,false,false,false,true)==Pass::None);
    CHECK(draw(0x6e46c324,false,false,false,false,400)==Pass::None);
    CHECK(draw(0x6e46c324,false,false,false,false,4,2)==Pass::None);
    CHECK(draw(0x6e46c324,false,false,false,false,4,1,false)==Pass::None);
    CHECK(draw(0x6e46c324,false,false,false,false,4,1,true,0x6699b5d9)==Pass::None);
    for (const float aspect:{21.0f/9.0f,32.0f/9.0f,3440.0f/1440.0f}) {
        // Captured title conversion P11=2.41421 and P00=P11/aspect. Restore the
        // offscreen conversion, not the later UI composition, to native projection.
        const float vertical=2.41421f;
        const float correction=G::KnackVideoConversionScale(aspect);
        CHECK(std::abs(vertical/aspect*correction-vertical/G::NativeAspect)<0.00001f);
        for (const float width:{1920.0f,2560.0f,3840.0f}) {
            const float center=width*0.5f;
            const float squeezed_left=center-center/correction;
            const float squeezed_right=center+center/correction;
            CHECK(std::abs(center+(squeezed_left-center)*correction)<0.001f);
            CHECK(std::abs(center+(squeezed_right-center)*correction-width)<0.001f);
            const auto bounds=G::KnackVideoContentBounds(int(width),aspect);
            CHECK(bounds[0]>0 && bounds[1]<int(width));
            CHECK(bounds[0]==int(width)-bounds[1]);
            CHECK(float(bounds[1]-bounds[0])<=width/correction);
            CHECK(width/correction-float(bounds[1]-bounds[0])<2.0f);
        }
    }
    CHECK(G::KnackVideoConversionScale(G::NativeAspect)==1.0f);
    CHECK(G::KnackVideoConversionScale(4.0f)==1.0f);
    CHECK(G::KnackVideoConversionScale(std::numeric_limits<float>::quiet_NaN())==1.0f);
    CHECK((G::KnackVideoContentBounds(1920,G::NativeAspect)==std::array<int,2>{0,1920}));
    CHECK((G::KnackVideoContentBounds(0,21.0f/9.0f)==std::array<int,2>{0,0}));
    for (const auto size:{G::ImageSize{2560,1080},G::ImageSize{3440,1440},G::ImageSize{3840,1600},G::ImageSize{5120,1440}}) {
        const float aspect=float(size.width)/size.height;
        const auto fitted=G::FitPresentation(1920,1080,size.width,size.height,aspect);
        CHECK(std::abs(fitted.width-size.width)<=1 && std::abs(fitted.height-size.height)<=1);
    }
    const auto normal=G::FitPresentation(1920,1080,3440,1440,G::NativeAspect);
    CHECK(normal.width==2560 && normal.height==1440);
    CHECK(G::FitPresentation(0,1080,3440,1440,21.0f/9.0f).width==0);
    CHECK(G::FitPresentation(1920,1080,3440,1440,std::numeric_limits<float>::quiet_NaN()).width==2560);
    std::puts("Ultrawide: monitor selection, presentation PASS");
}
