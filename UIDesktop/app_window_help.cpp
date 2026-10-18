#include "mainwindow.h"
#include <QTextBrowser>
#include <QLineEdit>
#include <QListWidget>
#include <QSplitter>
#include <QFile>
#include <QRegularExpression>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextCursor>
#include <QUrl>
#include <QFontMetrics>
#include <QStyle>

void MainWindow::app_show_user_guide()
{
    if(m_userGuide) {m_userGuide->show();m_userGuide->raise();return;}
    m_userGuide=new QDialog(this);m_userGuide->setObjectName("offlineUserGuide");
    m_userGuide->setWindowTitle("TempHumi User Guide");m_userGuide->resize(940,620);
    m_userGuide->setAttribute(Qt::WA_QuitOnClose,false);m_userGuide->setModal(false);
    auto layout=new QVBoxLayout(m_userGuide);
    auto searchRow=new QHBoxLayout;auto search=new QLineEdit(m_userGuide);search->setObjectName("guideSearch");search->setPlaceholderText("Find in this offline guide");
    auto next=new QPushButton("Find next",m_userGuide);next->setObjectName("guideFindNext");searchRow->addWidget(search);searchRow->addWidget(next);layout->addLayout(searchRow);
    auto splitter=new QSplitter(Qt::Horizontal,m_userGuide);splitter->setObjectName("guideSplitter");
    auto chapters=new QListWidget(splitter);chapters->setObjectName("guideChapters");chapters->setWordWrap(false);
    chapters->setTextElideMode(Qt::ElideRight);
    chapters->setMinimumWidth(150);chapters->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto browser=new QTextBrowser(splitter);browser->setObjectName("userGuideContent");browser->setOpenExternalLinks(false);
    browser->document()->setDefaultFont(m_userGuide->font());
    splitter->addWidget(chapters);splitter->addWidget(browser);splitter->setStretchFactor(0,1);splitter->setStretchFactor(1,4);
    layout->addWidget(splitter);
    QFile resource(":/help/user_guide.html");QString html;
    if(resource.open(QIODevice::ReadOnly)) html=QString::fromUtf8(resource.readAll());
    const QRegularExpression headings("<h2 id=\"section([0-9]+)\">(.*?)</h2>");
    const QStringList navigationTitles={"Getting started","UART settings","Draw / Start","REAL and SIM",
        "Channel sources","Curves and Y axes","X window","Wheel and drag","Point Inspector","Events and time",
        "Pause and Measure","Temperature fits","Export","Recovery & restore","End / Exit","Storage and limits",
        "Simulation","Measurement limits"};
    auto matches=headings.globalMatch(html);QVector<int> starts;QStringList pages;int navigationTextWidth=0;
    const QFontMetrics navigationMetrics(chapters->font());
    while(matches.hasNext()) {
        const auto match=matches.next();starts.append(match.capturedStart());const int section=match.captured(1).toInt();
        const QString title=section>=1 &&section<=navigationTitles.size()
            ?QString::number(section)+". "+navigationTitles[section-1]:match.captured(2);
        auto item=new QListWidgetItem(title,chapters);item->setToolTip(match.captured(2));item->setData(Qt::UserRole,match.captured(2));
        navigationTextWidth=qMax(navigationTextWidth,navigationMetrics.horizontalAdvance(title));
    }
    // Reserve the style's frame/focus/scrollbar width plus a small text inset.
    // Keep the roughly20/80 split, expanding only enough for complete single-line labels.
    const int navigationPadding=2*chapters->frameWidth()+2*chapters->style()->pixelMetric(QStyle::PM_FocusFrameHMargin)
        +chapters->style()->pixelMetric(QStyle::PM_ScrollBarExtent)+8;
    const int navigationWidth=qMax(180,navigationTextWidth+navigationPadding);
    chapters->setMinimumWidth(qMax(150,navigationTextWidth+navigationPadding));
    splitter->setSizes({navigationWidth,qMax(500,900-navigationWidth)});
    for(int n=0;n<starts.size();++n) pages.append(html.mid(starts[n],(n+1<starts.size()?starts[n+1]:html.indexOf("</body>"))-starts[n]));
    m_userGuide->setProperty("embeddedGuideLoaded",pages.size()==18);m_userGuide->setProperty("guideChapterCount",pages.size());
    connect(chapters,&QListWidget::currentRowChanged,browser,[this,browser,pages](int row) {
        if(row<0 ||row>=pages.size()) return;
        browser->setHtml("<html><body>"+pages[row]+"</body></html>");
        browser->document()->setBaseUrl(QUrl("qrc:/help/"));browser->moveCursor(QTextCursor::Start);
        m_userGuide->setProperty("currentGuideChapter",row+1);
    });
    chapters->setCurrentRow(0);
    const auto find=[search,browser,chapters]() {
        if(search->text().isEmpty()) return;
        for(int visited=0;visited<=chapters->count();++visited) {
            if(browser->find(search->text())) return;
            if(chapters->count()==0) return;
            chapters->setCurrentRow((chapters->currentRow()+1)%chapters->count());
        }
    };
    connect(next,&QPushButton::clicked,browser,find);connect(search,&QLineEdit::returnPressed,browser,find);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Close,m_userGuide);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,m_userGuide,&QDialog::hide);m_userGuide->show();app_write_status();
}
