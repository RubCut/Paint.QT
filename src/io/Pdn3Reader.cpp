#include "io/Pdn3Reader.h"

#include "core/BlendMode.h"
#include "core/Layer.h"
#include "core/Surface.h"

#include <QHash>
#include <QStringList>
#include <QtEndian>

#include <zlib.h>

namespace pnq {

namespace {

// --- NRBF type codes ---------------------------------------------------------
// Values from the BinaryFormatter layout Paint.NET writes with. Every value in
// the stream starts with a record type byte, so these are the grammar itself.

enum RecordType : quint8 {
    RecSerializedStreamHeader = 0,
    RecClassWithId = 1,
    RecSystemClassWithMembers = 2,
    RecClassWithMembers = 3,
    RecSystemClassWithMembersAndTypes = 4,
    RecClassWithMembersAndTypes = 5,
    RecBinaryObjectString = 6,
    RecBinaryArray = 7,
    RecMemberPrimitiveTyped = 8,
    RecMemberReference = 9,
    RecObjectNull = 10,
    RecMessageEnd = 11,
    RecBinaryLibrary = 12,
    RecObjectNullMultiple256 = 13,
    RecObjectNullMultiple = 14,
    RecArraySinglePrimitive = 15,
    RecArraySingleObject = 16,
    RecArraySingleString = 17,
    RecMethodCall = 21,
    RecMethodReturn = 22,
};

enum PrimitiveType : quint8 {
    PrimBoolean = 1,
    PrimByte = 2,
    PrimChar = 3,
    PrimDouble = 6,
    PrimInt16 = 7,
    PrimInt32 = 8,
    PrimInt64 = 9,
    PrimSByte = 10,
    PrimSingle = 11,
    PrimTimeSpan = 12,
    PrimDateTime = 13,
    PrimUInt16 = 14,
    PrimUInt32 = 15,
    PrimUInt64 = 16,
    PrimString = 18,
};

enum BinaryType : quint8 {
    BinPrimitive = 0,
    BinString = 1,
    BinObject = 2,
    BinSystemClass = 3,
    BinClass = 4,
    BinObjectArray = 5,
    BinStringArray = 6,
    BinPrimitiveArray = 7,
};

enum BinaryArrayType : quint8 {
    ArrSingle = 0,
    ArrJagged = 1,
    ArrRectangular = 2,
    ArrSingleOffset = 3,
    ArrJaggedOffset = 4,
    ArrRectangularOffset = 5,
};

/// One value from the stream.
///
/// An NRBF stream is a graph, not a tree: a member holding an object is stored as
/// an id pointing at something read earlier. So values cannot be returned as plain
/// data while parsing. Each carries the id it was filed under, and references are
/// followed once the whole stream has been read, which is the only order in which
/// that can work.
struct Value
{
    enum class Kind {
        Null,
        Bool,
        Int,
        Real,
        Text,
        Blob,
        Object,
        List,
        Reference,
    };

    Kind kind = Kind::Null;
    qint64 integer = 0;
    double real = 0.0;
    QString text;        ///< for Text, and the class name for Object
    QByteArray blob;     ///< for Blob
    QVector<Value> items;///< members of an Object, elements of a List
    QStringList names;   ///< member names of an Object, parallel to items
    quint32 id = 0;
    quint32 ref = 0;

    bool isNull() const { return kind == Kind::Null; }
    bool isObject() const { return kind == Value::Kind::Object; }

    /// The named member of an object, or nullptr. Nulls count as absent, because a
    /// null here and a missing key mean the same thing to the caller.
    const Value* find(const char* name) const
    {
        if (kind != Value::Kind::Object)
            return nullptr;
        for (int i = 0; i < names.size() && i < items.size(); ++i) {
            if (names.at(i) == QLatin1String(name) && !items.at(i).isNull())
                return &items.at(i);
        }
        return nullptr;
    }

    /// The named member as a number, or fallback when it is missing or not a number.
    qint64 number(const char* name, qint64 fallback = 0) const
    {
        const Value* v = find(name);
        if (!v)
            return fallback;
        switch (v->kind) {
        case Kind::Int:
        case Kind::Bool:
            return v->integer;
        case Kind::Real:
            return qint64(v->real);
        default:
            return fallback;
        }
    }

    QString string(const char* name) const
    {
        const Value* v = find(name);
        return (v && v->kind == Kind::Text) ? v->text : QString();
    }
};

/// A class definition: name, member names, and for the "AndTypes" variants the
/// member types too.
struct ClassInfo
{
    quint32 id = 0;
    QString name;
    QStringList members;
    QVector<quint8> types;
    QVector<int64_t> extra;
};

/// Reads the object graph, and then keeps reading the same bytes, because the
/// pixel data for the layers follows the graph in the same stream.
class NrbfReader
{
public:
    NrbfReader(const QByteArray& bytes, QString* error) : m_b(bytes), m_error(error) {}

    bool run(Value* root)
    {
        const quint8 tag = u8();
        if (tag != RecSerializedStreamHeader) {
            fail(QObject::tr("it does not begin the way a Paint.NET file should"));
            return false;
        }
        const qint32 rootId = leI32();
        (void)leI32();  // header id
        const qint32 major = leI32();
        const qint32 minor = leI32();
        if (m_failed)
            return false;
        if (major != 1 || minor != 0) {
            fail(QObject::tr("it uses a version of this format this program does not know"));
            return false;
        }
        if (rootId == 0) {
            fail(QObject::tr("it stores the document as a method call, which this program does not read"));
            return false;
        }
        m_rootId = quint32(rootId);

        for (;;) {
            if (atEnd()) {
                fail(QObject::tr("it ends before the document is complete"));
                return false;
            }
            const quint8 rec = u8();
            if (m_failed)
                return false;
            if (rec == RecMessageEnd)
                break;
            readRecord(rec);
            if (m_failed)
                return false;
        }

        followReferences();
        if (m_failed)
            return false;
        if (!m_objects.contains(m_rootId)) {
            fail(QObject::tr("it names no document"));
            return false;
        }
        *root = m_objects.value(m_rootId);
        return true;
    }

    bool failed() const { return m_failed; }

    /// Reads one layer's pixel block from wherever the stream now stands.
    ///
    /// Paint.NET writes these straight after the object graph, one per layer, in
    /// the same order the layers appear in the document. So this is called as many
    /// times as there are layers, and it simply keeps advancing.
    bool readPixelBlock(int expected, QByteArray* out)
    {
        if (atEnd()) {
            fail(QObject::tr("the image data ends early"));
            return false;
        }
        const quint8 format = u8();
        if (format != 0 && format != 1) {
            fail(QObject::tr("the image data uses a compression this program does not know"));
            return false;
        }
        const quint32 chunkSize = beU32();
        if (m_failed)
            return false;
        if (chunkSize == 0 || qint64(chunkSize) > Pdn3Reader::MaxPixels) {
            fail(QObject::tr("the image data declares an impossible chunk size"));
            return false;
        }
        const qint64 chunks = (qint64(expected) + chunkSize - 1) / chunkSize;
        if (chunks <= 0 || chunks > 4'000'000) {
            fail(QObject::tr("the image data declares an impossible number of chunks"));
            return false;
        }

        QByteArray dst(expected, Qt::Uninitialized);
        QVector<bool> seen(int(chunks), false);

        for (qint64 i = 0; i < chunks; ++i) {
            if (atEnd()) {
                fail(QObject::tr("the image data is truncated"));
                return false;
            }
            const quint32 number = beU32();
            if (m_failed)
                return false;
            if (qint64(number) >= chunks) {
                fail(QObject::tr("the image data names a chunk that does not exist"));
                return false;
            }
            if (seen[int(number)]) {
                fail(QObject::tr("the image data repeats a chunk"));
                return false;
            }
            seen[int(number)] = true;

            const quint32 dataSize = beU32();
            if (m_failed)
                return false;
            const QByteArray raw = take(int(dataSize));
            if (m_failed)
                return false;

            const qint64 offset = qint64(number) * chunkSize;
            qint64 want = qint64(chunkSize);
            if (offset + want > expected)
                want = qint64(expected) - offset;

            QByteArray plain;
            if (format == 0) {
                plain = gunzip(raw, int(want));
                if (plain.isEmpty() && want > 0) {
                    fail(QObject::tr("a block of image data could not be decompressed"));
                    return false;
                }
            } else {
                plain = raw;
            }
            if (qint64(plain.size()) != want) {
                fail(QObject::tr("a block of image data is the wrong size"));
                return false;
            }
            dst.replace(int(offset), int(want), plain);
        }
        *out = dst;
        return true;
    }

private:
    // --- primitives --------------------------------------------------------

    bool atEnd() const { return m_p >= m_b.size(); }

    void fail(const QString& what)
    {
        if (m_error && m_error->isEmpty()) {
            // The byte offset is kept in the message on purpose: a file that will not
            // parse is worth knowing where it stopped at, and without it the only way
            // to find out is to print a trace.
            *m_error = QObject::tr("This Paint.NET file could not be read: %1 (at byte %2)")
                           .arg(what)
                           .arg(m_p);
        }
        m_failed = true;
    }

    quint8 u8()
    {
        if (m_p >= m_b.size()) {
            fail(QObject::tr("it ends in the middle of a value"));
            return 0;
        }
        return quint8(m_b.at(m_p++));
    }

    quint16 leU16()
    {
        if (m_p + 2 > m_b.size()) {
            fail(QObject::tr("it ends in the middle of a number"));
            return 0;
        }
        const quint16 v = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(m_b.constData() + m_p));
        m_p += 2;
        return v;
    }

    qint32 leI32()
    {
        if (m_p + 4 > m_b.size()) {
            fail(QObject::tr("it ends in the middle of a number"));
            return 0;
        }
        const qint32 v = qFromLittleEndian<qint32>(
            reinterpret_cast<const uchar*>(m_b.constData() + m_p));
        m_p += 4;
        return v;
    }

    qint64 leI64()
    {
        if (m_p + 8 > m_b.size()) {
            fail(QObject::tr("it ends in the middle of a number"));
            return 0;
        }
        const qint64 v = qFromLittleEndian<qint64>(
            reinterpret_cast<const uchar*>(m_b.constData() + m_p));
        m_p += 8;
        return v;
    }

    /// The image blocks use big-endian lengths, unlike the object graph.
    quint32 beU32()
    {
        if (m_p + 4 > m_b.size()) {
            fail(QObject::tr("it ends in the middle of a number"));
            return 0;
        }
        const quint32 v = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(m_b.constData() + m_p));
        m_p += 4;
        return v;
    }

    QByteArray take(int n)
    {
        if (n < 0 || m_p + n > m_b.size()) {
            fail(QObject::tr("it ends in the middle of a block of data"));
            return QByteArray();
        }
        const QByteArray out = m_b.mid(m_p, n);
        m_p += n;
        return out;
    }

    /// Lengths are seven bits per byte, most significant group first, with the top
    /// bit of each byte meaning another byte follows. One byte covers nearly every
    /// length here; the loop is what handles the rest.
    quint32 length()
    {
        quint32 v = 0;
        int shift = 0;
        for (int i = 0; i < 5; ++i) {
            const quint8 b = u8();
            v |= quint32(b & 0x7F) << shift;
            if ((b & 0x80) == 0)
                return v;
            shift += 7;
        }
        fail(QObject::tr("a length in the file is too long to be valid"));
        return 0;
    }

    QString text()
    {
        const quint32 n = length();
        if (m_failed)
            return QString();
        const QByteArray raw = take(int(n));
        if (m_failed)
            return QString();
        return QString::fromUtf8(raw);
    }

    static QByteArray gunzip(const QByteArray& in, int expected)
    {
        // The stream is a raw deflate with a gzip wrapper, so the window size has to
        // be asked for; a plain file would carry its own header. Room is left over
        // because the block may decompress to exactly the chunk and no less.
        z_stream zs{};
        if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)
            return QByteArray();
        QByteArray out(qMax(expected, 1) + 1024, Qt::Uninitialized);
        zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
        zs.avail_in = uInt(in.size());
        zs.next_out = reinterpret_cast<Bytef*>(out.data());
        zs.avail_out = uInt(out.size());
        const int r = inflate(&zs, Z_FINISH);
        const uLong got = uLong(out.size() - zs.avail_out);
        inflateEnd(&zs);
        if (r != Z_STREAM_END && r != Z_OK)
            return QByteArray();
        out.resize(int(got));
        return out;
    }

    // --- grammar -----------------------------------------------------------

    void store(const Value& v)
    {
        if (v.id != 0)
            m_objects.insert(v.id, v);
    }

    /// Reads one record. Returns false when the record was bookkeeping (a library
    /// or a run of nulls) that the caller should step over rather than store.
    bool readRecord(quint8 rec, Value* out = nullptr)
    {
        Value v;
        switch (rec) {
        case RecClassWithId: {
            const qint32 objectId = leI32();
            const qint32 metadataId = leI32();
            const ClassInfo ci = m_classes.value(quint32(metadataId));
            v = readMembers(&ci, quint32(objectId), true);
            break;
        }
        case RecSystemClassWithMembers:
        case RecClassWithMembers: {
            const ClassInfo ci = readClassInfo();
            if (rec == RecClassWithMembers)
                (void)leI32();  // library id
            v = readMembers(&ci, ci.id, false);
            break;
        }
        case RecSystemClassWithMembersAndTypes:
        case RecClassWithMembersAndTypes: {
            ClassInfo ci = readClassInfo();
            readMemberTypes(&ci);
            if (rec == RecClassWithMembersAndTypes)
                (void)leI32();  // library id
            v = readMembers(&ci, ci.id, true);
            break;
        }
        case RecBinaryObjectString: {
            const qint32 id = leI32();
            v.kind = Value::Kind::Text;
            v.text = text();
            v.id = quint32(id);
            break;
        }
        case RecBinaryArray:
            v = readBinaryArray();
            break;
        case RecMemberPrimitiveTyped: {
            const quint8 prim = u8();
            v = readPrimitive(prim);
            break;
        }
        case RecMemberReference: {
            const qint32 ref = leI32();
            v.kind = Value::Kind::Reference;
            v.ref = quint32(ref);
            if (out)
                *out = v;
            return true;  // a reference is filed under the id it points at
        }
        case RecObjectNull:
            return false;
        case RecObjectNullMultiple256:
        case RecObjectNullMultiple:
            return false;  // the caller steps over the run by peeking again
        case RecArraySinglePrimitive: {
            const qint32 id = leI32();
            const qint32 n = leI32();
            const quint8 prim = u8();
            v = readPrimitive(prim, n);
            v.id = quint32(id);
            break;
        }
        case RecArraySingleObject:
        case RecArraySingleString: {
            const qint32 id = leI32();
            const qint32 n = leI32();
            v = readObjectArray(quint32(id), n);
            break;
        }
        case RecBinaryLibrary:
            (void)leI32();
            (void)text();
            return false;
        case RecMethodCall:
        case RecMethodReturn:
            fail(QObject::tr("it uses a part of the format this program does not read"));
            return false;
        default:
            fail(QObject::tr("it contains a record this program does not recognise"));
            return false;
        }
        if (m_failed)
            return false;
        if (out)
            *out = v;
        store(v);
        return true;
    }

    /// Reads the next value for a member or array slot, stepping over libraries and
    /// runs of nulls. Returns false only on failure; a null member is a success.
    bool readSlot(Value* out)
    {
        for (;;) {
            if (atEnd()) {
                fail(QObject::tr("it ends in the middle of a value"));
                return false;
            }
            const quint8 rec = u8();
            if (rec == RecBinaryLibrary) {
                (void)leI32();
                (void)text();
                if (m_failed)
                    return false;
                continue;
            }
            if (rec == RecObjectNullMultiple256) {
                const int n = u8();
                if (m_failed)
                    return false;
                // Each null in the run stands for a member or element, so the caller
                // has to be told to skip them rather than have them read here.
                *out = Value();
                out->kind = Value::Kind::Null;
                out->integer = 0;
                m_pendingNulls = qMax(0, n - 1);
                return true;
            }
            if (rec == RecObjectNullMultiple) {
                const qint32 n = leI32();
                if (m_failed)
                    return false;
                *out = Value();
                m_pendingNulls = qMax(0, int(n) - 1);
                return true;
            }
            if (rec == RecObjectNull) {
                *out = Value();
                return true;
            }
            if (!readRecord(rec, out)) {
                if (m_failed)
                    return false;
                *out = Value();
            }
            return true;
        }
    }

    int takePendingNulls()
    {
        const int n = m_pendingNulls;
        m_pendingNulls = 0;
        return n;
    }

    ClassInfo readClassInfo()
    {
        ClassInfo ci;
        ci.id = quint32(leI32());
        ci.name = text();
        const qint32 count = leI32();
        if (m_failed || count < 0 || count > 4096) {
            fail(QObject::tr("a class in the file claims an impossible number of members"));
            return ci;
        }
        for (qint32 i = 0; i < count; ++i)
            ci.members << text();
        if (m_failed)
            return ci;
        m_classes.insert(ci.id, ci);
        return ci;
    }

    void readMemberTypes(ClassInfo* ci)
    {
        // All the type bytes come first, as one run, and only then does the extra
        // information for each of them follow. Reading them in pairs instead puts
        // every extra byte in the wrong place, and the failure shows up much later
        // as a class that will not parse.
        ci->types.reserve(ci->members.size());
        for (int i = 0; i < ci->members.size(); ++i)
            ci->types << u8();
        ci->extra.reserve(ci->members.size());

        for (int i = 0; i < ci->types.size(); ++i) {
            const quint8 t = ci->types.at(i);
            ci->extra << -1;
            switch (t) {
            case BinPrimitive:
            case BinPrimitiveArray:
                // Which primitive, named in a byte of its own.
                ci->extra[i] = qint64(u8());
                break;
            case BinSystemClass:
                (void)text();
                break;
            case BinClass:
                (void)text();   // the class name
                (void)leI32();  // the library it was declared in
                break;
            case BinString:
            case BinObject:
            case BinObjectArray:
            case BinStringArray:
                break;
            default:
                fail(QObject::tr("a member has a type this program does not recognise"));
                return;
            }
        }
    }

    Value readMembers(const ClassInfo* ci, quint32 objectId, bool typed)
    {
        Value v;
        v.kind = Value::Kind::Object;
        v.id = objectId;
        v.text = ci ? ci->name : QString();
        v.names = ci ? ci->members : QStringList();

        for (int i = 0; i < v.names.size(); ++i) {
            if (i < takePendingNulls())
                continue;

            quint8 type = BinObject;
            int64_t prim = -1;
            if (typed && ci && i < ci->types.size()) {
                type = ci->types.at(i);
                prim = ci->extra.value(i, -1);
            }

            Value member;
            if (type == BinPrimitive && prim >= 0) {
                member = readPrimitive(quint8(prim));
            } else if (type == BinString) {
                member.kind = Value::Kind::Text;
                member.text = text();
            } else if (type == BinPrimitiveArray) {
                const qint32 n = leI32();
                member = readPrimitive(quint8(prim), n);
            } else {
                if (!readSlot(&member))
                    return v;
            }
            if (m_failed)
                return v;
            v.items << member;
        }
        return v;
    }

    Value readBinaryArray()
    {
        const qint32 id = leI32();
        const quint8 arrayType = u8();
        const qint32 rank = leI32();
        if (m_failed || rank < 0 || rank > 8) {
            fail(QObject::tr("an array in the file claims an impossible rank"));
            return Value();
        }
        qint64 total = 1;
        for (qint32 i = 0; i < rank; ++i) {
            const qint32 len = leI32();
            if (m_failed)
                return Value();
            if (len < 0 || (len > 0 && total > 64'000'000 / qMax(1, len))) {
                fail(QObject::tr("an array in the file claims an impossible length"));
                return Value();
            }
            total *= len;
        }
        if (arrayType == ArrSingleOffset || arrayType == ArrJaggedOffset
            || arrayType == ArrRectangularOffset) {
            for (qint32 i = 0; i < rank; ++i)
                (void)leI32();
        }
        const quint8 binaryType = u8();
        int64_t prim = -1;
        if (binaryType == BinPrimitive)
            prim = qint64(u8());
        if (m_failed)
            return Value();

        Value v;
        if (binaryType == BinPrimitive) {
            v = readPrimitive(quint8(prim), int(total));
        } else {
            v = readObjectArray(quint32(id), int(total));
        }
        v.id = quint32(id);
        return v;
    }

    Value readObjectArray(quint32 id, int count)
    {
        Value v;
        v.kind = Value::Kind::List;
        v.id = id;
        for (int i = 0; i < count; ++i) {
            if (i < takePendingNulls()) {
                v.items << Value();
                continue;
            }
            Value item;
            if (!readSlot(&item))
                return v;
            v.items << item;
        }
        return v;
    }

    Value readPrimitive(quint8 prim, int count = -1)
    {
        Value v;
        const bool many = count >= 0;
        switch (prim) {
        case PrimBoolean:
            if (many) {
                v.kind = Value::Kind::Blob;
                v.blob = take(count);
                return v;
            }
            v.kind = Value::Kind::Bool;
            v.integer = (u8() != 0) ? 1 : 0;
            return v;
        case PrimByte:
        case PrimSByte:
            if (many) {
                v.kind = Value::Kind::Blob;
                v.blob = take(count);
                return v;
            }
            v.kind = Value::Kind::Int;
            v.integer = (prim == PrimByte) ? qint64(u8()) : qint64(qint8(u8()));
            return v;
        case PrimChar:
            v.kind = Value::Kind::Int;
            v.integer = qint64(leU16());
            return v;
        case PrimInt16:
            if (many) {
                v.kind = Value::Kind::Blob;
                v.blob = take(2 * count);
                return v;
            }
            v.kind = Value::Kind::Int;
            v.integer = qint64(qint16(leU16()));
            return v;
        case PrimUInt16:
            if (many) {
                v.kind = Value::Kind::Blob;
                v.blob = take(2 * count);
                return v;
            }
            v.kind = Value::Kind::Int;
            v.integer = qint64(leU16());
            return v;
        case PrimInt32:
            if (many) {
                v.kind = Value::Kind::Blob;
                v.blob = take(4 * count);
                return v;
            }
            v.kind = Value::Kind::Int;
            v.integer = qint64(leI32());
            return v;
        case PrimUInt32:
            if (many) {
                v.kind = Value::Kind::Blob;
                v.blob = take(4 * count);
                return v;
            }
            v.kind = Value::Kind::Int;
            v.integer = qint64(quint32(leI32()));
            return v;
        case PrimInt64:
            if (many) {
                v.kind = Value::Kind::Blob;
                v.blob = take(8 * count);
                return v;
            }
            v.kind = Value::Kind::Int;
            v.integer = leI64();
            return v;
        case PrimUInt64:
            if (many) {
                v.kind = Value::Kind::Blob;
                v.blob = take(8 * count);
                return v;
            }
            v.kind = Value::Kind::Int;
            v.integer = leI64();
            return v;
        case PrimSingle:
            v.kind = Value::Kind::Real;
            if (m_p + 4 <= m_b.size()) {
                float f = 0.0f;
                memcpy(&f, m_b.constData() + m_p, 4);
                v.real = double(f);
            }
            m_p = qMin(m_p + 4, m_b.size());
            return v;
        case PrimDouble:
            v.kind = Value::Kind::Real;
            if (m_p + 8 <= m_b.size()) {
                double d = 0.0;
                memcpy(&d, m_b.constData() + m_p, 8);
                v.real = d;
            }
            m_p = qMin(m_p + 8, m_b.size());
            return v;
        case PrimTimeSpan:
        case PrimDateTime:
            v.kind = Value::Kind::Int;
            v.integer = leI64();
            return v;
        case PrimString:
            v.kind = Value::Kind::Text;
            v.text = text();
            return v;
        default:
            fail(QObject::tr("it stores a value of a type this program does not recognise"));
            return v;
        }
    }

    /// Follows every reference to the object it names. Several passes, because an
    /// object can itself be a reference and one pass would leave some unresolved.
    void followReferences()
    {
        for (int pass = 0; pass < 8; ++pass) {
            bool changed = false;
            for (auto it = m_objects.begin(); it != m_objects.end(); ++it) {
                Value copy = it.value();
                if (followIn(copy, 0)) {
                    it.value() = copy;
                    changed = true;
                }
            }
            if (!changed)
                break;
        }
        // The root itself may be a reference.
        if (m_objects.contains(m_rootId)) {
            Value root = m_objects.value(m_rootId);
            if (root.kind == Value::Kind::Reference && m_objects.contains(root.ref)) {
                m_objects.insert(m_rootId, m_objects.value(root.ref));
            }
        }
    }

    bool followIn(Value& v, int depth)
    {
        if (depth > 40)
            return false;
        bool changed = false;
        if (v.kind == Value::Kind::Reference) {
            auto it = m_objects.constFind(v.ref);
            if (it != m_objects.constEnd() && it.value().kind != Value::Kind::Reference) {
                v = it.value();
                changed = true;
            }
        }
        for (Value& item : v.items) {
            if (followIn(item, depth + 1))
                changed = true;
        }
        return changed;
    }

    QByteArray m_b;
    int m_p = 0;
    bool m_failed = false;
    int m_pendingNulls = 0;
    quint32 m_rootId = 0;
    QString* m_error = nullptr;
    QHash<quint32, Value> m_objects;
    QHash<quint32, ClassInfo> m_classes;
};

/// Maps a Paint.NET blend operation onto ours.
///
/// Paint.NET's own enum is a different order and a different set from ours, so the
/// numbers cannot be cast across. Modes with no counterpart here fall back to the
/// closest one this program has; the alternative would be to lose the layer's
/// appearance entirely.
BlendMode mapBlend(int paintNetMode)
{
    switch (paintNetMode) {
    case 0: return BlendMode::Normal;
    case 1: return BlendMode::Multiply;
    case 2: return BlendMode::LinearDodge;   // Additive
    case 3: return BlendMode::ColorBurn;
    case 4: return BlendMode::ColorDodge;
    case 5: return BlendMode::HardLight;      // Reflect has no exact counterpart
    case 6: return BlendMode::LinearLight;    // Glow likewise
    case 7: return BlendMode::Overlay;
    case 8: return BlendMode::Difference;
    case 9: return BlendMode::Exclusion;      // Negation
    case 10: return BlendMode::Lighten;
    case 11: return BlendMode::Darken;
    case 12: return BlendMode::Screen;
    case 13: return BlendMode::HardMix;       // XOR has no exact counterpart
    default: return BlendMode::Normal;
    }
}

/// The name Paint.NET gives a blend operation object, for files that store the
/// operation as a type instead of a number. Older versions do this.
int blendFromClassName(const QString& className)
{
    const QString n = className;
    if (n.contains(QLatin1String("MultiplyBlendOp"))) return 1;
    if (n.contains(QLatin1String("AdditiveBlendOp"))) return 2;
    if (n.contains(QLatin1String("ColorBurnBlendOp"))) return 3;
    if (n.contains(QLatin1String("ColorDodgeBlendOp"))) return 4;
    if (n.contains(QLatin1String("ReflectBlendOp"))) return 5;
    if (n.contains(QLatin1String("GlowBlendOp"))) return 6;
    if (n.contains(QLatin1String("OverlayBlendOp"))) return 7;
    if (n.contains(QLatin1String("DifferenceBlendOp"))) return 8;
    if (n.contains(QLatin1String("NegationBlendOp"))) return 9;
    if (n.contains(QLatin1String("LightenBlendOp"))) return 10;
    if (n.contains(QLatin1String("DarkenBlendOp"))) return 11;
    if (n.contains(QLatin1String("ScreenBlendOp"))) return 12;
    if (n.contains(QLatin1String("XorBlendOp"))) return 13;
    return 0;
}

/// Reads a layer's name, visibility, opacity and blend mode out of whichever of the
/// two shapes the file uses, and applies them.
void applyProperties(const Value& layerObj, Layer* layer)
{
    // The properties live in a nested object in the files that carry one, and
    // directly on the layer in the rest.
    const Value* props = layerObj.find("Layer_properties");
    if (!props)
        props = layerObj.find("properties");
    if (!props)
        props = &layerObj;

    if (const Value* n = props->find("name")) {
        if (n->kind == Value::Kind::Text && !n->text.isEmpty())
            layer->setName(n->text);
    }
    if (const Value* v = props->find("visible"))
        layer->setVisible(v->kind == Value::Kind::Bool ? v->integer != 0 : v->integer != 0);
    if (const Value* b = props->find("isBackground"))
        layer->setBackground(b->integer != 0);
    if (const Value* o = props->find("opacity"))
        layer->setOpacity(int(qBound(qint64(0), o->integer, qint64(255))));

    // Blend mode: a number in the newer files, a typed object in the older ones.
    if (const Value* bm = props->find("blendMode")) {
        if (bm->kind == Value::Kind::Int || bm->kind == Value::Kind::Bool)
            layer->setBlendMode(mapBlend(int(bm->integer)));
        else if (bm->isObject())
            layer->setBlendMode(mapBlend(blendFromClassName(bm->text)));
    } else if (const Value* op = props->find("blendOp")) {
        if (op->isObject())
            layer->setBlendMode(mapBlend(blendFromClassName(op->text)));
    } else if (const Value* ops = props->find("blendOps")) {
        if (ops->isObject())
            layer->setBlendMode(mapBlend(blendFromClassName(ops->text)));
    }
}

} // namespace

bool Pdn3Reader::looksLikePaintNet(const QByteArray& data)
{
    return data.size() >= 4 && data.startsWith("PDN3");
}

Document* Pdn3Reader::load(const QByteArray& data, QString* error)
{
    if (!looksLikePaintNet(data)) {
        if (error)
            *error = QObject::tr("Not a Paint.NET file");
        return nullptr;
    }
    if (data.size() < 16) {
        if (error)
            *error = QObject::tr("This Paint.NET file is too short to be one");
        return nullptr;
    }

    // "PDN3", then the XML length. Paint.NET writes that length in three bytes and
    // the fourth byte of the word is the first character of the XML, so it is
    // assembled here rather than read as one little-endian 32 bit value.
    const quint32 xmlLen = quint32(quint8(data[4])) | (quint32(quint8(data[5])) << 8)
        | (quint32(quint8(data[6])) << 16);
    if (qint64(xmlLen) > qint64(data.size() - 10)) {
        if (error)
            *error = QObject::tr("This Paint.NET file is truncated");
        return nullptr;
    }
    int p = 7 + int(xmlLen);
    if (p + 2 > data.size()) {
        if (error)
            *error = QObject::tr("This Paint.NET file is truncated");
        return nullptr;
    }
    // The two bytes the format puts between the XML and the object graph.
    if (quint8(data[p]) != 0x00 || quint8(data[p + 1]) != 0x01) {
        if (error)
            *error = QObject::tr("This file is not a Paint.NET project file");
        return nullptr;
    }
    p += 2;

    NrbfReader reader(data.mid(p), error);
    Value root;
    if (!reader.run(&root)) {
        if (error && error->isEmpty())
            *error = QObject::tr("This Paint.NET file could not be read");
        return nullptr;
    }

    const int w = int(root.number("width"));
    const int h = int(root.number("height"));
    if (w <= 0 || h <= 0 || w > MaxDimension || h > MaxDimension
        || qint64(w) * h > MaxPixels) {
        if (error)
            *error = QObject::tr("This Paint.NET file has image dimensions this program will not open");
        return nullptr;
    }

    // The layer list is an ArrayList whose backing array is padded with nulls past
    // its size, so the entries are taken by identity rather than by what is in the
    // array. Files that store a plain list work too.
    QVector<const Value*> layerObjs;
    if (const Value* layers = root.find("layers")) {
        if (layers->kind == Value::Kind::Object) {
            for (const Value& f : layers->items) {
                if (f.kind == Value::Kind::List) {
                    for (const Value& it : f.items) {
                        if (it.isObject())
                            layerObjs << &it;
                    }
                    break;
                }
            }
        } else if (layers->kind == Value::Kind::List) {
            for (const Value& it : layers->items) {
                if (it.isObject())
                    layerObjs << &it;
            }
        }
    }
    if (layerObjs.isEmpty()) {
        if (error)
            *error = QObject::tr("This Paint.NET file contains no layers this program can see");
        return nullptr;
    }
    if (layerObjs.size() > MaxLayers) {
        if (error)
            *error = QObject::tr("This file has more layers than this program will open");
        return nullptr;
    }

    // Each layer declares its own pixel length and stride; the data follows the
    // object graph in the same stream, one block per layer.
    QVector<qint64> declared(layerObjs.size(), 0);
    for (int i = 0; i < layerObjs.size(); ++i) {
        const Value* lo = layerObjs.at(i);
        const Value* surface = lo->find("surface");
        const Value* scan = surface ? surface->find("scan0") : nullptr;
        if (!scan)
            scan = lo->find("scan0");
        qint64 len = scan ? scan->number("length64") : 0;
        if (len <= 0) {
            // Some files name the length differently; the canvas size is the
            // fallback, since a layer that covers the whole document is the norm.
            len = qint64(w) * h * 4;
        }
        if (len > qint64(MaxPixels) * 4) {
            if (error)
                *error = QObject::tr("A layer in this Paint.NET file is larger than this program will open");
            return nullptr;
        }
        declared[i] = len;
    }

    Document* doc = new Document(w, h);
    QString why;
    for (int i = 0; i < layerObjs.size(); ++i) {
        QByteArray pixels;
        if (!reader.readPixelBlock(int(declared.at(i)), &pixels)) {
            why = (error && !error->isEmpty()) ? *error : QString();
            break;
        }
        Surface surf(w, h);
        if (!fillSurface(pixels, declared.at(i), w, h, &surf, &why))
            break;
        Layer* l = new Layer(w, h);
        l->setSurface(surf);
        applyProperties(*layerObjs.at(i), l);
        doc->insertLayer(l, -1);
    }

    if (!why.isEmpty() || doc->layerCount() == 0) {
        delete doc;
        if (error) {
            *error = why.isEmpty()
                ? QObject::tr("The image data in this Paint.NET file could not be read")
                : why;
        }
        return nullptr;
    }

    doc->setDirty(false);
    return doc;
}

bool Pdn3Reader::fillSurface(const QByteArray& pixels, qint64 length, int w, int h, Surface* out,
                           QString* error)
{
    if (!out)
        return false;
    const qint64 need = qint64(w) * h * 4;
    if (length < need) {
        if (error)
            *error = QObject::tr("A layer in this Paint.NET file holds less pixel data than its size needs");
        return false;
    }

    // The bytes are stored as BGRA with the top row first. Swapping the colour
    // channels across and taking the alpha out of them gives what a Surface holds.
    const quint32 stride = quint32(w) * 4;

    for (int y = 0; y < h; ++y) {
        quint32* out32 = reinterpret_cast<quint32*>(out->scanLine(y));
        if (!out32) {
            if (error)
                *error = QObject::tr("A layer in this Paint.NET file could not be held in memory");
            return false;
        }
        const quint8* src = reinterpret_cast<const quint8*>(pixels.constData()) + qint64(y) * stride;
        for (int x = 0; x < w; ++x) {
            const quint8 b = src[x * 4 + 0];
            const quint8 g = src[x * 4 + 1];
            const quint8 r = src[x * 4 + 2];
            const quint8 a = src[x * 4 + 3];
            // Premultiplied, because a Surface stores premultiplied pixels.
            const quint32 pr = quint32(qBound(0, int(r) - int(a), 255));
            const quint32 pg = quint32(qBound(0, int(g) - int(a), 255));
            const quint32 pb = quint32(qBound(0, int(b) - int(a), 255));
            out32[x] = (quint32(a) << 24) | (pr << 16) | (pg << 8) | pb;
        }
    }
    return true;
}

} // namespace pnq
