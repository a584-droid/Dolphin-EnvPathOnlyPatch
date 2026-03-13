/*
    This file is part of the KDE project
    SPDX-FileCopyrightText: 2020 Felix Ernst <felixernst@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "dolphinurlnavigator.h"

#include "dolphin_generalsettings.h"
#include "dolphinplacesmodelsingleton.h"
#include "dolphinurlnavigatorscontroller.h"
#include "global.h"

#include <KLocalizedString>
#include <KUrlComboBox>

#include <QAbstractButton>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QProcessEnvironment>

DolphinUrlNavigator::DolphinUrlNavigator(QWidget *parent)
    : DolphinUrlNavigator(QUrl(), parent)
{
}

DolphinUrlNavigator::DolphinUrlNavigator(const QUrl &url, QWidget *parent)
    : KUrlNavigator(DolphinPlacesModelSingleton::instance().placesModel(), url, parent)
{
    const GeneralSettings *settings = GeneralSettings::self();
    setUrlEditable(settings->editableUrl());
    setShowFullPath(settings->showFullPath());
    setHomeUrl(Dolphin::homeUrl());
    setPlacesSelectorVisible(DolphinUrlNavigatorsController::placesSelectorVisible());
    editor()->setCompletionMode(KCompletion::CompletionMode(settings->urlCompletionMode()));
    setWhatsThis(xi18nc("@info:whatsthis location bar",
                        "<para>This describes the location of the files and folders "
                        "displayed below.</para><para>The name of the currently viewed "
                        "folder can be read at the very right. To the left of it is the "
                        "name of the folder that contains it. The whole line is called "
                        "the <emphasis>path</emphasis> to the current location because "
                        "following these folders from left to right leads here.</para>"
                        "<para>This interactive path "
                        "is more powerful than one would expect. To learn more "
                        "about the basic and advanced features of the location bar "
                        "<link url='help:/dolphin/location-bar.html'>click here</link>. "
                        "This will open the dedicated page in the Handbook.</para>"));

    DolphinUrlNavigatorsController::registerDolphinUrlNavigator(this);

    connect(this, &KUrlNavigator::returnPressed, this, &DolphinUrlNavigator::slotReturnPressed);
    editor()->lineEdit()->installEventFilter(this);

    auto readOnlyBadge = new QLabel();
    readOnlyBadge->setPixmap(QIcon::fromTheme(QStringLiteral("emblem-readonly")).pixmap(12, 12));
    readOnlyBadge->setToolTip(i18nc("@info:tooltip of a 'locked' symbol in url navigator", "This folder is not writable for you."));
    readOnlyBadge->hide();
    setBadgeWidget(readOnlyBadge);
}

DolphinUrlNavigator::~DolphinUrlNavigator()
{
    DolphinUrlNavigatorsController::unregisterDolphinUrlNavigator(this);
}

QSize DolphinUrlNavigator::sizeHint() const
{
    if (isUrlEditable()) {
        return editor()->lineEdit()->sizeHint();
    }
    int widthHint = 0;
    for (int i = 0; i < layout()->count(); ++i) {
        QWidget *widget = layout()->itemAt(i)->widget();
        const QAbstractButton *button = qobject_cast<QAbstractButton *>(widget);
        if (button && button->icon().isNull()) {
            widthHint += widget->minimumSizeHint().width();
        }
    }
    if (readOnlyBadgeVisible()) {
        widthHint += badgeWidget()->sizeHint().width();
    }
    return QSize(widthHint, KUrlNavigator::sizeHint().height());
}

std::unique_ptr<DolphinUrlNavigator::VisualState> DolphinUrlNavigator::visualState() const
{
    std::unique_ptr<VisualState> visualState{new VisualState};
    visualState->isUrlEditable = (isUrlEditable());
    const QLineEdit *lineEdit = editor()->lineEdit();
    visualState->hasFocus = lineEdit->hasFocus();
    visualState->text = lineEdit->text();
    visualState->cursorPosition = lineEdit->cursorPosition();
    visualState->selectionStart = lineEdit->selectionStart();
    visualState->selectionLength = lineEdit->selectionLength();
    return visualState;
}

void DolphinUrlNavigator::setVisualState(const VisualState &visualState)
{
    setUrlEditable(visualState.isUrlEditable);
    if (!visualState.isUrlEditable) {
        return;
    }
    editor()->lineEdit()->setText(visualState.text);
    if (visualState.hasFocus) {
        editor()->lineEdit()->setFocus();
        editor()->lineEdit()->setCursorPosition(visualState.cursorPosition);
        if (visualState.selectionStart != -1) {
            editor()->lineEdit()->setSelection(visualState.selectionStart, visualState.selectionLength);
        }
    }
}

void DolphinUrlNavigator::clearText() const
{
    editor()->lineEdit()->clear();
}

void DolphinUrlNavigator::setPlaceholderText(const QString &text)
{
    editor()->lineEdit()->setPlaceholderText(text);
}

void DolphinUrlNavigator::setReadOnlyBadgeVisible(bool visible)
{
    QWidget *readOnlyBadge = badgeWidget();
    if (readOnlyBadge) {
        readOnlyBadge->setVisible(visible);
    }
}

bool DolphinUrlNavigator::readOnlyBadgeVisible() const
{
    QWidget *readOnlyBadge = badgeWidget();
    if (readOnlyBadge) {
        return readOnlyBadge->isVisible();
    }
    return false;
}

void DolphinUrlNavigator::slotReturnPressed()
{
    if (!GeneralSettings::editableUrl()) {
        setUrlEditable(false);
    }
}

bool DolphinUrlNavigator::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != editor()->lineEdit() || event->type() != QEvent::KeyPress) {
        return KUrlNavigator::eventFilter(watched, event);
    }

    const auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->key() != Qt::Key_Return && keyEvent->key() != Qt::Key_Enter) {
        return KUrlNavigator::eventFilter(watched, event);
    }

    const QString userInput = editor()->lineEdit()->text();
    if (!userInput.startsWith(QLatin1Char('$'))) {
        return KUrlNavigator::eventFilter(watched, event);
    }

    int variableNameEnd = 1;
    while (variableNameEnd < userInput.size()) {
        const QChar ch = userInput.at(variableNameEnd);
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_')) {
            break;
        }
        ++variableNameEnd;
    }

    const QString variableName = userInput.mid(1, variableNameEnd - 1);
    if (variableName.isEmpty()) {
        return KUrlNavigator::eventFilter(watched, event);
    }

    const QString variableValue = QProcessEnvironment::systemEnvironment().value(variableName);
    if (variableValue.isEmpty()) {
        return KUrlNavigator::eventFilter(watched, event);
    }

    const QString suffix = userInput.mid(variableNameEnd);
    QList<QUrl> urls;
    constexpr int maxPathsPerWindow = 10;
    const QStringList envPaths = variableValue.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const QString &envPath : envPaths) {
        if (!(QDir::isAbsolutePath(envPath) || envPath.startsWith(QStringLiteral("~/")))) {
            // Ignore non-path entries (e.g. values accidentally mixed into path lists)
            // and keep processing valid path entries.
            continue;
        }

        const QString basePath = envPath.startsWith(QStringLiteral("~/")) ? QDir::home().filePath(envPath.mid(2)) : envPath;
        const QString candidatePath = QDir::cleanPath(basePath + suffix);
        if (QFileInfo::exists(candidatePath)) {
            urls.append(QUrl::fromLocalFile(candidatePath));
        }
    }

    if (urls.isEmpty()) {
        return KUrlNavigator::eventFilter(watched, event);
    }

    setLocationUrl(urls.takeFirst());
    constexpr int maxAdditionalTabs = maxPathsPerWindow - 1;
    for (int i = 0; i < urls.size(); ++i) {
        if (i < maxAdditionalTabs) {
            Q_EMIT tabRequested(urls.at(i));
            continue;
        }

        Dolphin::openNewWindow(urls.mid(i, maxPathsPerWindow), this);
        i += maxPathsPerWindow - 1;
    }
    return true;
}

void DolphinUrlNavigator::keyPressEvent(QKeyEvent *keyEvent)
{
    if (keyEvent->key() == Qt::Key_Escape && !isUrlEditable()) {
        Q_EMIT requestToLoseFocus();
        return;
    }
    KUrlNavigator::keyPressEvent(keyEvent);
}

#include "moc_dolphinurlnavigator.cpp"
