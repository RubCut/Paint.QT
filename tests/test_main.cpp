#include <QtTest>

#include "core/BlendMode.h"
#include "core/Brush.h"
#include "core/ColorUtils.h"
#include "core/Document.h"
#include "core/Gradient.h"
#include "core/History.h"
#include "core/ImageMath.h"
#include "core/ImageOps.h"
#include "core/Renderer.h"
#include "core/Selection.h"
#include "core/Surface.h"
#include "effects/Effects.h"
#include "io/PdnFile.h"
#include "resources/Palettes.h"

using namespace pnq;

class TestCore : public QObject
{
    Q_OBJECT
private slots:
    // ------------------------------------------------------------- surface
    void surface_basics()
    {
        Surface s(4, 3);
        QCOMPARE(s.width(), 4);
        QCOMPARE(s.height(), 3);
        s.fill(rgbPixel(10, 20, 30));
        QCOMPARE(getR(s.pixel(2, 2)), 10);
        QCOMPARE(getG(s.pixel(2, 2)), 20);
        QCOMPARE(getB(s.pixel(2, 2)), 30);
        QCOMPARE(getA(s.pixel(2, 2)), 255);
        // out of bounds reads are safe
        QCOMPARE(s.pixel(-1, 0), 0u);
        QCOMPARE(s.pixel(100, 100), 0u);
    }

    void surface_alpha_roundtrip()
    {
        for (int a = 64; a < 256; a += 17) {
            const pixel_t p = qPremult(quint8(a), 200, 100, 50);
            QCOMPARE(int(getA(p)), a);
            // 8 bit premultiplied storage loses a little precision at low alpha.
            QVERIFY(qAbs(int(getR(p)) - 200) <= 4);
            QVERIFY(qAbs(int(getG(p)) - 100) <= 4);
            QVERIFY(qAbs(int(getB(p)) - 50) <= 4);
        }
    }

    // ------------------------------------------------------------- blending
    void blend_normal_opaque_over_transparent()
    {
        const pixel_t src = rgbPixel(10, 20, 30);
        const pixel_t dst = 0;
        const pixel_t out = composePixel(dst, src, BlendMode::Normal);
        QCOMPARE(getR(out), 10);
        QCOMPARE(getA(out), 255);
    }

    void blend_normal_over_white()
    {
        // 50% red over white -> 255,127,127
        const pixel_t dst = rgbPixel(255, 255, 255);
        const pixel_t src = qPremult(128, 255, 0, 0);
        const pixel_t out = composePixel(dst, src, BlendMode::Normal);
        QCOMPARE(int(getR(out)), 255);
        QVERIFY(qAbs(int(getG(out)) - 127) <= 1);
        QVERIFY(qAbs(int(getB(out)) - 127) <= 1);
    }

    void blend_normal_transparent_is_noop()
    {
        const pixel_t dst = rgbPixel(1, 2, 3);
        QCOMPARE(composePixel(dst, 0, BlendMode::Normal), dst);
    }

    void blend_multiply()
    {
        // Multiply of 0.5*0.5 = 0.25 -> 64
        const pixel_t dst = rgbPixel(128, 128, 128);
        const pixel_t src = rgbPixel(128, 128, 128);
        const pixel_t out = composePixel(dst, src, BlendMode::Multiply);
        QVERIFY(qAbs(int(getR(out)) - 64) <= 2);
    }

    void blend_screen()
    {
        const pixel_t dst = rgbPixel(128, 0, 0);
        const pixel_t src = rgbPixel(128, 0, 0);
        const pixel_t out = composePixel(dst, src, BlendMode::Screen);
        // 128 + 128 - 64 = 192
        QVERIFY(qAbs(int(getR(out)) - 192) <= 2);
    }

    void blend_darken_lighten()
    {
        const pixel_t a = rgbPixel(60, 60, 60);
        const pixel_t b = rgbPixel(200, 200, 200);
        QCOMPARE(int(getR(composePixel(a, b, BlendMode::Darken))), 60);
        QCOMPARE(int(getR(composePixel(a, b, BlendMode::Lighten))), 200);
    }

    void blend_difference()
    {
        const pixel_t a = rgbPixel(200, 100, 50);
        const pixel_t b = rgbPixel(50, 200, 100);
        const pixel_t out = composePixel(a, b, BlendMode::Difference);
        QCOMPARE(int(getR(out)), 150);
        QCOMPARE(int(getG(out)), 100);
        QCOMPARE(int(getB(out)), 50);
    }

    void blend_luminosity_keeps_perceived_luminance()
    {
        const pixel_t dst = rgbPixel(128, 128, 128);
        const pixel_t src = rgbPixel(200, 50, 50);
        const pixel_t out = composePixel(dst, src, BlendMode::Luminosity);
        const double lumIn = 0.3 * 200 + 0.59 * 50 + 0.11 * 50;
        const double lumOut = 0.3 * getR(out) + 0.59 * getG(out) + 0.11 * getB(out);
        QVERIFY(qAbs(lumIn - lumOut) < 6.0);
    }

    void blend_mode_names_and_keys()
    {
        QCOMPARE(blendModeKey(BlendMode::Multiply), QStringLiteral("multiply"));
        BlendMode m;
        QVERIFY(blendModeFromKey(QStringLiteral("softlight"), &m));
        QCOMPARE(m, BlendMode::SoftLight);
        QVERIFY(!blendModeFromKey(QStringLiteral("nope"), &m));
        QCOMPARE(allBlendModes().size(), int(BlendMode::Count));
    }

    // ------------------------------------------------------------- color
    void hsv_roundtrip()
    {
        for (int h = 0; h < 360; h += 23) {
            for (int s = 0; s <= 255; s += 85) {
                for (int v = 0; v <= 255; v += 85) {
                    int r, g, b;
                    hsvToRgbInt(h, s, v, &r, &g, &b);
                    int h2, s2, v2;
                    rgbToHsvInt(r, g, b, &h2, &s2, &v2);
                    QCOMPARE(v2, v);
                    if (s == 0 || v == 0)
                        continue; // hue/saturation are undefined
                    // 8 bit RGB quantisation costs hue accuracy, and the coarser
                    // the colour the worse: one RGB step spans many degrees at
                    // low saturation.
                    const int dh = qMin(qAbs(h2 - h), 360 - qAbs(h2 - h));
                    const int tol = s >= 170 ? 1 : (s >= 128 ? 2 : 4);
                    QVERIFY2(dh <= tol, qPrintable(QString("hue %1 -> %2 (s=%3)").arg(h).arg(h2).arg(s)));
                    QVERIFY(qAbs(s2 - s) <= 2);
                }
            }
        }
    }

    void hex_roundtrip()
    {
        bool ok = false;
        const pixel_t p = colorFromHex(QStringLiteral("#3A7BC8"), &ok);
        QVERIFY(ok);
        QCOMPARE(int(getR(p)), 0x3A);
        QCOMPARE(int(getG(p)), 0x7B);
        QCOMPARE(int(getB(p)), 0xC8);
        QCOMPARE(colorToHex(p), QStringLiteral("#3a7bc8"));
    }

    // ------------------------------------------------------------- document
    void document_layer_lifecycle()
    {
        Document doc(16, 16);
        QCOMPARE(doc.layerCount(), 0);
        doc.addLayer();
        QCOMPARE(doc.layerCount(), 1);
        doc.addLayer();
        QCOMPARE(doc.layerCount(), 2);
        doc.setActiveLayerIndex(0);
        doc.duplicateLayerAt(0);
        QCOMPARE(doc.layerCount(), 3);
        doc.removeLayerAt(1);
        QCOMPARE(doc.layerCount(), 2);
        // The last layer can never be removed.
        doc.removeLayerAt(0);
        doc.removeLayerAt(0);
        QCOMPARE(doc.layerCount(), 1);
    }

    void document_canvas_resize()
    {
        Document doc(10, 20);
        Layer* l = doc.addLayer();
        l->surface().fill(rgbPixel(255, 0, 0));
        doc.resizeCanvas(20, 40, Qt::AlignLeft | Qt::AlignTop);
        QCOMPARE(doc.width(), 20);
        QCOMPARE(doc.height(), 40);
        QCOMPARE(l->width(), 20);
        QCOMPARE(getR(l->surface().pixel(0, 0)), 255);
        // Undo restores the previous state.
        doc.history()->undo();
        QCOMPARE(doc.width(), 10);
        QCOMPARE(doc.height(), 20);
        doc.history()->redo();
        QCOMPARE(doc.width(), 20);
    }

    void document_flatten_drops_alpha()
    {
        Document doc(4, 4);
        Layer* l = doc.addLayer();
        l->setSurface(Surface(4, 4)); // fully transparent
        doc.flattenImage();
        QCOMPARE(doc.layerCount(), 1);
        QCOMPARE(int(getA(doc.layerAt(0)->surface().pixel(0, 0))), 255);
    }

    // ------------------------------------------------------------- history
    void history_undo_redo_pixels()
    {
        Document doc(8, 8);
        Layer* l = doc.addLayer();
        l->surface().fill(rgbPixel(0, 0, 0));
        doc.history()->clear();

        const QRect r(2, 2, 3, 3);
        const Surface before = l->surface().cropped(r);
        l->surface().fillRect(r, rgbPixel(255, 0, 0));
        const Surface after = l->surface().cropped(r);
        doc.history()->push(new PixelDeltaAction(QStringLiteral("test"), 0, r, before, after));
        QCOMPARE(int(getR(l->surface().pixel(3, 3))), 255);
        doc.history()->undo();
        QCOMPARE(int(getR(l->surface().pixel(3, 3))), 0);
        doc.history()->redo();
        QCOMPARE(int(getR(l->surface().pixel(3, 3))), 255);
    }

    void history_clear_and_jump()
    {
        Document doc(4, 4);
        doc.addLayer();
        doc.history()->clear();
        for (int i = 0; i < 3; ++i) {
            const QRect r(0, 0, 4, 4);
            const Surface before = doc.layerAt(0)->surface().copy();
            doc.layerAt(0)->surface().fill(rgbPixel(i * 10, 0, 0));
            doc.history()->push(
                new SurfaceAction(QStringLiteral("step%1").arg(i), 0, before,
                                  doc.layerAt(0)->surface().copy()));
        }
        QCOMPARE(doc.history()->count(), 3);
        doc.history()->jumpTo(0);
        QCOMPARE(doc.history()->currentIndex(), 0);
        QCOMPARE(int(getR(doc.layerAt(0)->surface().pixel(0, 0))), 0);
        doc.history()->jumpTo(2);
        QCOMPARE(int(getR(doc.layerAt(0)->surface().pixel(0, 0))), 20);
    }

    void history_max_length_trims()
    {
        Document doc(4, 4);
        doc.addLayer();
        doc.setMaxHistoryLength(5);
        doc.history()->clear();
        for (int i = 0; i < 12; ++i) {
            const Surface before = doc.layerAt(0)->surface().copy();
            doc.layerAt(0)->surface().fill(rgbPixel(i, 0, 0));
            doc.history()->push(
                new SurfaceAction(QStringLiteral("s%1").arg(i), 0, before,
                                  doc.layerAt(0)->surface().copy()));
        }
        QVERIFY(doc.history()->count() <= 5);
    }

    // ------------------------------------------------------------- selection
    void selection_rect_and_bounds()
    {
        Selection s(10, 10);
        s.selectRect(QRect(2, 3, 4, 5));
        QCOMPARE(s.at(3, 4), 255);
        QCOMPARE(s.at(0, 0), 0);
        QCOMPARE(s.nonEmptyRect(), QRect(2, 3, 4, 5));
    }

    void selection_boolean_ops()
    {
        Selection a, b(10, 10);
        a.selectRect(QRect(0, 0, 5, 10));
        b.selectRect(QRect(2, 0, 5, 10));
        Selection add = a;
        add += b;
        QCOMPARE(int(add.at(6, 5)), 255);
        Selection sub = a;
        sub -= b;
        QCOMPARE(int(sub.at(6, 5)), 0);
        QCOMPARE(int(sub.at(1, 5)), 255);
        Selection inter = a;
        inter &= b;
        QCOMPARE(int(inter.at(1, 5)), 0);
        QCOMPARE(int(inter.at(3, 5)), 255);
    }

    void selection_invert()
    {
        Selection s(4, 4);
        s.selectRect(QRect(0, 0, 2, 2));
        s.invert();
        QCOMPARE(int(s.at(0, 0)), 0);
        QCOMPARE(int(s.at(3, 3)), 255);
    }

    void selection_magic_wand_on_solid()
    {
        QImage img(8, 8, QImage::Format_ARGB32);
        img.fill(QColor(10, 20, 30));
        Selection s;
        s.magicWand(img, QPoint(4, 4), 0, true);
        QCOMPARE(s.at(4, 4), 255);
        QCOMPARE(s.at(0, 0), 255);
    }

    // ------------------------------------------------------------- renderer
    void renderer_composites_layers()
    {
        Document doc(4, 4);
        Layer* a = doc.addLayer();
        a->surface().fill(qPremult(128, 255, 0, 0));
        Layer* b = doc.addLayer();
        b->setVisible(false);
        b->surface().fill(rgbPixel(0, 0, 255));
        const Surface comp = doc.compositeSurface();
        QCOMPARE(int(getR(comp.pixel(0, 0))), 255);
        QCOMPARE(int(getB(comp.pixel(0, 0))), 0);
        b->setVisible(true);
        const Surface comp2 = doc.compositeSurface();
        QVERIFY(int(getB(comp2.pixel(0, 0))) > 0);
    }

    void renderer_layer_opacity()
    {
        Document doc(2, 2);
        Layer* a = doc.addLayer();
        a->surface().fill(rgbPixel(0, 0, 0));
        Layer* b = doc.addLayer();
        b->setOpacity(128);
        b->surface().fill(rgbPixel(255, 255, 255));
        const Surface comp = doc.compositeSurface();
        QVERIFY(qAbs(int(getR(comp.pixel(0, 0))) - 128) <= 2);
    }

    // ------------------------------------------------------------- brush
    void brush_stamp_center_is_opaque()
    {
        Brush b(16);
        b.setHardness(100);
        b.setOpacity(100);
        const QImage& stamp = b.stamp();
        QCOMPARE(stamp.width(), 16);
        QCOMPARE(int(qAlpha(stamp.pixel(8, 8))), 255);
    }

    void brush_softness_reduces_center_alpha()
    {
        Brush hard(32);
        hard.setHardness(100);
        Brush soft(32);
        soft.setHardness(0);
        QVERIFY(qAlpha(soft.stamp().pixel(16, 16)) < qAlpha(hard.stamp().pixel(16, 16)));
    }

    void brush_draws_on_surface()
    {
        Surface s(32, 32);
        Brush b(10);
        b.setHardness(100);
        b.drawPoint(s, QPointF(16, 16), rgbPixel(255, 0, 0), nullptr);
        QCOMPARE(int(getR(s.pixel(16, 16))), 255);
        QCOMPARE(int(getA(s.pixel(0, 0))), 0);
    }

    void brush_erase_clears_alpha()
    {
        Surface s(32, 32);
        s.fill(rgbPixel(0, 0, 0));
        Brush b(12);
        b.setMode(BrushMode::Erase);
        b.drawPoint(s, QPointF(16, 16), rgbPixel(255, 255, 255), nullptr);
        QVERIFY(getA(s.pixel(16, 16)) < 255);
    }

    // ------------------------------------------------------------- imagemath
    void imagemath_flood_fill()
    {
        Surface s(16, 16);
        s.fill(rgbPixel(0, 0, 0));
        ImageMath::floodFill(s, QPoint(8, 8), rgbPixel(255, 0, 0), 0, 255, nullptr);
        QCOMPARE(int(getR(s.pixel(8, 8))), 255);
        QCOMPARE(int(getR(s.pixel(0, 0))), 255);
    }

    void imagemath_blur_moves_colors()
    {
        Surface s(16, 16);
        s.clear();
        s.fillRect(QRect(0, 0, 8, 16), rgbPixel(255, 0, 0));
        ImageMath::convolve(s, ImageMath::gaussianKernel(6.0f));
        // The right half is now contaminated.
        QVERIFY(getA(s.pixel(12, 8)) > 0);
    }

    void imagemath_box_blur()
    {
        Surface s(9, 9);
        s.fill(rgbPixel(0, 0, 0));
        s.fillRect(QRect(4, 4, 1, 1), rgbPixel(255, 255, 255));
        ImageMath::boxBlur(s, 2);
        QVERIFY(int(getR(s.pixel(4, 4))) > 0);
    }

    // ------------------------------------------------------------- imageops
    void imageops_flip()
    {
        Surface s(2, 1);
        s.setPixel(0, 0, rgbPixel(255, 0, 0));
        s.setPixel(1, 0, rgbPixel(0, 255, 0));
        ImageOps::flipHorizontal(s);
        QCOMPARE(int(getR(s.pixel(0, 0))), 0);
        QCOMPARE(int(getG(s.pixel(0, 0))), 255);
    }

    void imageops_rotate90_swaps_dimensions()
    {
        Surface s(4, 2);
        ImageOps::rotate90(s, false);
        QCOMPARE(s.width(), 2);
        QCOMPARE(s.height(), 4);
    }

    void imageops_resize()
    {
        Surface s(10, 10);
        s.fill(rgbPixel(1, 2, 3));
        ImageOps::resizeSurface(s, 20, 20, true, ImageOps::ResizeMode::Normal);
        QCOMPARE(s.width(), 20);
        QCOMPARE(s.height(), 20);
        QCOMPARE(int(getR(s.pixel(10, 10))), 1);
    }

    void imageops_paste_into_layer()
    {
        Document doc(10, 10);
        doc.addLayer();
        Surface clip(4, 4);
        clip.fill(rgbPixel(0, 255, 0));
        ImageOps::pasteIntoLayer(doc, clip, 2, 2, 0);
        QCOMPARE(int(getG(doc.layerAt(0)->surface().pixel(3, 3))), 255);
        QCOMPARE(int(getA(doc.layerAt(0)->surface().pixel(0, 0))), 0);
        // undo
        doc.history()->undo();
        QCOMPARE(int(getA(doc.layerAt(0)->surface().pixel(3, 3))), 0);
    }

    // ------------------------------------------------------------- effects
    void effects_invert()
    {
        Surface s(2, 2);
        s.fill(rgbPixel(0, 0, 0));
        Effects::invert(s, Selection());
        QCOMPARE(int(getR(s.pixel(0, 0))), 255);
    }

    void effects_brightness()
    {
        Surface s(2, 2);
        s.fill(rgbPixel(100, 100, 100));
        Effects::brightness(s, Selection(), 50);
        QVERIFY(getR(s.pixel(0, 0)) > 100);
    }

    void effects_respect_selection()
    {
        Surface s(4, 4);
        s.fill(rgbPixel(0, 0, 0));
        Selection sel(4, 4);
        sel.selectRect(QRect(0, 0, 2, 4));
        Effects::invert(s, sel);
        QCOMPARE(int(getR(s.pixel(0, 0))), 255); // inside
        QCOMPARE(int(getR(s.pixel(3, 0))), 0);   // outside
    }

    void effects_gaussian_blur_preserves_mean()
    {
        Surface s(16, 16);
        s.fill(rgbPixel(100, 100, 100));
        Effects::blurGaussian(s, Selection(), 2.0, false, false);
        QVERIFY(qAbs(int(getR(s.pixel(8, 8))) - 100) < 3);
    }

    void effects_threshold_is_binary()
    {
        Surface s(4, 4);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                s.setPixel(x, y, rgbPixel(x * 60, x * 60, x * 60));
        Effects::threshold(s, Selection(), 100);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                const int v = getR(s.pixel(x, y));
                QVERIFY(v == 0 || v == 255);
            }
        }
    }

    // ------------------------------------------------------------- gradient
    void gradient_two_color_interpolation()
    {
        const Gradient g = Gradient::fromTwoColors(0xFF000000u, 0xFFFFFFFFu);
        QCOMPARE(int(getR(g.colorAt(0.0))), 0);
        QCOMPARE(int(getR(g.colorAt(1.0))), 255);
        QVERIFY(qAbs(int(getR(g.colorAt(0.5))) - 128) <= 2);
    }

    void gradient_stops_sorted()
    {
        Gradient g;
        g.addStop(0.8, rgbPixel(1, 2, 3));
        g.addStop(0.2, rgbPixel(4, 5, 6));
        QVERIFY(g.stops().first().position <= g.stops().last().position);
    }

    // ------------------------------------------------------------- palettes
    void palettes_web_has_216_colors()
    {
        QCOMPARE(Palettes::web().size(), 216);
    }

    void palettes_builtin_present()
    {
        const auto all = Palettes::all();
        QVERIFY(all.size() >= 8);
        for (const Palette& p : all)
            QVERIFY(!p.name.isEmpty());
    }

    // ------------------------------------------------------------- pdn
    void pdn_roundtrip()
    {
        Document doc(8, 8);
        Layer* l = doc.addLayer(QStringLiteral("Layer A"));
        l->surface().fill(qPremult(200, 10, 20, 30));
        Layer* l2 = doc.addLayer(QStringLiteral("Layer B"));
        l2->surface().fill(rgbPixel(0, 255, 0));
        l2->setOpacity(128);
        l2->setBlendMode(BlendMode::Multiply);
        doc.setFilePath(QStringLiteral("/tmp/test.pdn"));

        QString err;
        QVERIFY2(PdnFile::save(doc, QStringLiteral("/tmp/pnq_test.pdn"), &err), qPrintable(err));
        Document* loaded = PdnFile::load(QStringLiteral("/tmp/pnq_test.pdn"), &err);
        QVERIFY2(loaded != nullptr, qPrintable(err));
        QCOMPARE(loaded->width(), 8);
        QCOMPARE(loaded->height(), 8);
        QCOMPARE(loaded->layerCount(), 2);
        QCOMPARE(loaded->layerAt(0)->name(), QStringLiteral("Layer A"));
        QCOMPARE(int(getA(loaded->layerAt(0)->surface().pixel(0, 0))), 200);
        QCOMPARE(int(getR(loaded->layerAt(0)->surface().pixel(0, 0))), 10);
        QCOMPARE(loaded->layerAt(1)->blendMode(), BlendMode::Multiply);
        QCOMPARE(loaded->layerAt(1)->opacity(), 128);
        delete loaded;
    }

    void pdn_rejects_garbage()
    {
        QString err;
        QCOMPARE(PdnFile::load(QStringLiteral("/tmp/definitely_missing.pdn"), &err), nullptr);
    }
};

QTEST_MAIN(TestCore)
#include "test_main.moc"
