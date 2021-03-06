#include "DisasmView.h"
#include "libasmx64.h"
#include "DebugCore.h"
#include "global.h"
#include "EventDispatcher.h"
#include "global.h"

#include <QtWidgets>

DisasmView::DisasmView(QWidget *parent)
    : QAbstractScrollArea(parent),
      m_regionStart(0),m_regionSize(0)
{
	QObject::connect(EventDispatcher::instance(), &EventDispatcher::setDebugCore, this, &DisasmView::setDebugCore);
	QObject::connect(EventDispatcher::instance(), &EventDispatcher::setDisasmAddress, this, &DisasmView::gotoAddress);
	QObject::connect(EventDispatcher::instance(), &EventDispatcher::debugEvent, this, &DisasmView::updateContent);
	QObject::connect(EventDispatcher::instance(), &EventDispatcher::refreshDisasmView,
					 viewport(), static_cast<void(QWidget::*)()>(&QWidget::update));
	QObject::connect(EventDispatcher::instance(), &EventDispatcher::breakpointChanged,
					 viewport(), static_cast<void(QWidget::*)()>(&QWidget::update));
}

void DisasmView::gotoAddress(uint64_t address)
{
	log(QString("on gotoAddress: %1").arg(address));
	if(address == 0)
	{
		return;
	}
    m_currentAddress = address;
    if (!(address >= m_regionStart && address <= (m_regionStart + m_regionSize)))
    {
        setRegion(address);
    }

    m_foundIndex = false;
    std::size_t i = 0;
    for (; i < m_insnStart.size(); ++i)
    {
        auto tmp = m_insnStart[i];
        if (tmp == address)
        {
            m_foundIndex = true;
            break;
        }
        else if (tmp > address)
        {
            break;
        }
    }

    verticalScrollBar()->setValue(i);
	viewport()->update();
}

void DisasmView::setRegion(uint64_t address)
{
	auto dbgcore = m_debugCore.lock();
    if (!dbgcore)
    {
        log("setRegion without debugcore ptr", LogType::Warning);
        return;
    }
    if (!dbgcore->findRegion(address, m_regionStart, m_regionSize))
    {
        log("DisasmView::setRegion findRegion failed", LogType::Error);
        return;
    }
    analysis();
}

void DisasmView::analysis()
{
	auto dbgcore = m_debugCore.lock();
	if (!dbgcore)
	{
		return;
	}
    log(QString("in analysis: %1, %2").arg(m_regionStart,0,16).arg(m_regionSize,0,16));
    std::vector<uint8_t> buf(m_regionSize);
    if (!dbgcore->readMemory(m_regionStart, buf.data(), buf.size()))
    {
        log("In DisasmView::analysis, readMemory failed", LogType::Warning);
        return;
    }

    uint64_t addr = m_regionStart;
    x64dis decoder;
    for (int i = 0;i < buf.size();)
    {
        x86dis_insn* insn = decoder.decode(buf.data() + i, buf.size() - i, addr);
        //const char* pcsIns = decoder.str(insn, DIS_STYLE_HEX_ASMSTYLE | DIS_STYLE_HEX_UPPERCASE | DIS_STYLE_HEX_NOZEROPAD | DIS_STYLE_SIGNED | X86DIS_STYLE_EXPLICIT_MEMSIZE);
        //printf("0x%016" PRIX64 "\t%s\n", addr, pcsIns);
        m_insnStart.emplace_back(addr);
        addr += insn->size;
        i += insn->size;
    }

    verticalScrollBar()->setMaximum(static_cast<int>(m_insnStart.size() - 1));
}

void DisasmView::paintEvent(QPaintEvent * e)
{
	auto dbgcore = m_debugCore.lock();
	if (!dbgcore)
	{
		return;
	}

    if (m_insnStart.size() <= verticalScrollBar()->value())
    {
        return;
    }
    QPainter p(viewport());

    x64dis decoder;
    uint64_t addr = 0;
    if (!m_foundIndex)
    {
        addr = m_currentAddress;
    }
    else
    {
        addr = m_insnStart[verticalScrollBar()->value()];
    }

    int h = viewport()->fontMetrics().height();
    for (int i = 0; i < viewport()->height(); i += h)
    {
        if ((m_regionStart + m_regionSize) <= addr)
        {
            break;
        }
        int size = std::min(15ull, m_regionStart + m_regionSize - addr);
        uint8_t buff[15];
        //FIXME:判断读取是否成功
		dbgcore->readMemory(addr, buff, size);
        x86dis_insn* insn = decoder.decode(buff, size, addr);
        const char* insnStr = decoder.str(insn, DIS_STYLE_HEX_ASMSTYLE | DIS_STYLE_HEX_UPPERCASE | DIS_STYLE_HEX_NOZEROPAD | DIS_STYLE_SIGNED | X86DIS_STYLE_EXPLICIT_MEMSIZE);
        //printf("0x%016" PRIX64 "\t%s\n", addr, pcsIns);

        QRect rc(0, i, viewport()->width(), h);
		auto bp = dbgcore->findBreakpoint(addr);
		if (bp)
		{
			if (bp->enabled())
			{
				p.fillRect(rc, Qt::red);
			}
			else
			{
				p.fillRect(rc, QColor(255, 170, 255));
			}
		}
		else if (addr == dbgcore->excAddr())
		{
			p.fillRect(rc, QColor(72, 118, 255));
		}
        else if (addr == g_highlightAddress)
        {
            p.fillRect(rc, Qt::lightGray);
        }
        p.drawText(rc, 0, QString::number(addr, 16).append("\t\t").append(insnStr));
        addr += insn->size;
    }

    QAbstractScrollArea::paintEvent(e);
}


void DisasmView::mousePressEvent(QMouseEvent *event)
{
	auto dbgcore = m_debugCore.lock();
	if (!dbgcore)
	{
		return;
	}

    if (m_insnStart.size() <= verticalScrollBar()->value())
    {
        return;
    }

    x64dis decoder;
    uint64_t addr = 0;
    if (!m_foundIndex)
    {
        addr = m_currentAddress;
    }
    else
    {
        addr = m_insnStart[verticalScrollBar()->value()];
    }

    int h = viewport()->fontMetrics().height();
    auto y = event->pos().y();
    for (int i = 0; i < viewport()->height(); i += h)
    {
        if ((m_regionStart + m_regionSize) <= addr)
        {
            break;
        }
        int size = std::min(15ull, m_regionStart + m_regionSize - addr);
        uint8_t buff[15];
        //FIXME:判断读取是否成功
		dbgcore->readMemory(addr, buff, size);
        x86dis_insn* insn = decoder.decode(buff, size, addr);

        if (y >= i && y < i + h)
        {
            g_highlightAddress = addr;
            break;
        }
        addr += insn->size;
    }

    viewport()->update();
    QAbstractScrollArea::mousePressEvent(event);
}

void DisasmView::setDebugCore(std::shared_ptr<DebugCore> debugCore)
{
    m_debugCore = debugCore;
}

DisasmView::~DisasmView()
{
}

void DisasmView::wheelEvent(QWheelEvent *event)
{
    m_foundIndex = true;
    QAbstractScrollArea::wheelEvent(event);
}

void DisasmView::updateContent()
{
	auto debugCore = m_debugCore.lock();
	if (!debugCore)
	{
		return;
	}

	gotoAddress(debugCore->excAddr());
}
