// krazy:excludeall=qclasses

//////////////////////////////////////////////////////////////////////////////
// oxygenwindowmanager.cpp
// pass some window mouse press/release/move event actions to window manager
// -------------------
//
// Copyright (c) 2010 Hugo Pereira Da Costa <hugo.pereira@free.fr>
//
// Largely inspired from BeSpin style
// Copyright (C) 2007 Thomas Luebking <thomas.luebking@web.de>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//////////////////////////////////////////////////////////////////////////////

#include "oxygenwindowmanager.h"
#include "oxygenwindowmanager.moc"
#include "oxygenpropertynames.h"
#include "oxygenstyleconfigdata.h"
#include "oxygenstylehelper.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QGraphicsView>
#include <QGroupBox>
#include <QLabel>
#include <QListView>
#include <QMainWindow>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QProgressBar>
#include <QScrollBar>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionGroupBox>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>

#include <QTextStream>

#if HAVE_X11
#include <QX11Info>
#endif

namespace Oxygen
{

    //_____________________________________________________________
    WindowManager::WindowManager( QObject* parent ):
        QObject( parent ),
        _enabled( true ),
        _useWMMoveResize( true ),
        _dragMode( StyleConfigData::WD_FULL ),
        _dragDistance( QApplication::startDragDistance() ),
        _dragDelay( QApplication::startDragTime() ),
        _dragAboutToStart( false ),
        _dragInProgress( false ),
        _locked( false ),
        _cursorOverride( false ),
        _isX11( false )
    {

        // install application wise event filter
        _appEventFilter = new AppEventFilter( this );
        qApp->installEventFilter( _appEventFilter );

        #if HAVE_X11
        _isX11 = QGuiApplication::platformName() == QStringLiteral("xcb");
        _moveResizeAtom = 0;
        if( _isX11 )
        {
            // create move-resize atom
            xcb_connection_t* connection( QX11Info::connection() );
            const QString atomName( QStringLiteral( "_NET_WM_MOVERESIZE" ) );
            xcb_intern_atom_cookie_t cookie( xcb_intern_atom( connection, false, atomName.size(), qPrintable( atomName ) ) );
            Helper::ScopedPointer<xcb_intern_atom_reply_t> reply( xcb_intern_atom_reply( connection, cookie, nullptr) );
            _moveResizeAtom = reply ? reply->atom:0;
        }
        #endif

    }

    //_____________________________________________________________
    void WindowManager::initialize( void )
    {

        setEnabled( StyleConfigData::windowDragEnabled() );
        setDragMode( StyleConfigData::windowDragMode() );
        setUseWMMoveResize( StyleConfigData::useWMMoveResize() );

        setDragDistance( QApplication::startDragDistance() );
        setDragDelay( QApplication::startDragTime() );

        initializeWhiteList();
        initializeBlackList();

    }

    //_____________________________________________________________
    void WindowManager::registerWidget( QWidget* widget )
    {

        if( isBlackListed( widget ) || isDragable( widget ) )
        {

            /*
            install filter for dragable widgets.
            also install filter for blacklisted widgets
            to be able to catch the relevant events and prevent
            the drag to happen
            */
            widget->removeEventFilter( this );
            widget->installEventFilter( this );

        }

    }

    //_____________________________________________________________
    void WindowManager::unregisterWidget( QWidget* widget )
    {
        if( widget )
        { widget->removeEventFilter( this ); }
    }

    //_____________________________________________________________
    void WindowManager::initializeWhiteList( void )
    {

        _whiteList.clear();

        // add user specified whitelisted classnames
        _whiteList.insert( ExceptionId( QStringLiteral( "MplayerWindow" ) ) );
        _whiteList.insert( ExceptionId( QStringLiteral( "ViewSliders@kmix" ) ) );
        _whiteList.insert( ExceptionId( QStringLiteral( "Sidebar_Widget@konqueror" ) ) );

        foreach( const QString& exception, StyleConfigData::windowDragWhiteList() )
        {
            ExceptionId id( exception );
            if( !id.className().isEmpty() )
            { _whiteList.insert( ExceptionId( exception ) ); }
        }
    }

    //_____________________________________________________________
    void WindowManager::initializeBlackList( void )
    {

        _blackList.clear();
        _blackList.insert( ExceptionId( QStringLiteral( "CustomTrackView@kdenlive" ) ) );
        _blackList.insert( ExceptionId( QStringLiteral( "MuseScore" ) ) );
        _blackList.insert( ExceptionId( QStringLiteral( "KGameCanvasWidget" ) ) );
        foreach( const QString& exception, StyleConfigData::windowDragBlackList() )
        {
            ExceptionId id( exception );
            if( !id.className().isEmpty() )
            { _blackList.insert( ExceptionId( exception ) ); }
        }

    }

    //_____________________________________________________________
    bool WindowManager::eventFilter( QObject* object, QEvent* event )
    {
        if( !enabled() ) return false;

        switch ( event->type() )
        {
            case QEvent::MouseButtonPress:
            return mousePressEvent( object, event );
            break;

            case QEvent::MouseMove:
            if ( object == _target.data() ) return mouseMoveEvent( object, event );
            break;

            case QEvent::MouseButtonRelease:
            if ( _target ) return mouseReleaseEvent( object, event );
            break;

            default:
            break;

        }

        return false;

    }

    //_____________________________________________________________
    void WindowManager::timerEvent( QTimerEvent* event )
    {

        if( event->timerId() == _dragTimer.timerId() )
        {

            _dragTimer.stop();
            if( _target )
            { startDrag( _target.data(), _globalDragPoint ); }

        } else {

            return QObject::timerEvent( event );

        }

    }

    //_____________________________________________________________
    bool WindowManager::mousePressEvent( QObject* object, QEvent* event )
    {

        // cast event and check buttons/modifiers
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>( event );
        if( !( mouseEvent->modifiers() == Qt::NoModifier && mouseEvent->button() == Qt::LeftButton ) )
        { return false; }

        // check lock
        if( isLocked() ) return false;
        else setLocked( true );

        // cast to widget
        QWidget *widget = static_cast<QWidget*>( object );

        // check if widget can be dragged from current position
        if( isBlackListed( widget ) || !canDrag( widget ) ) return false;

        // retrieve widget's child at event position
        QPoint position( mouseEvent->pos() );
        QWidget* child = widget->childAt( position );
        if( !canDrag( widget, child, position ) ) return false;

        // save target and drag point
        _target = widget;
        _dragPoint = position;
        _globalDragPoint = mouseEvent->globalPos();
        _dragAboutToStart = true;

        // send a move event to the current child with same position
        // if received, it is caught to actually start the drag
        QPoint localPoint( _dragPoint );
        if( child ) localPoint = child->mapFrom( widget, localPoint );
        else child = widget;
        QMouseEvent localMouseEvent( QEvent::MouseMove, localPoint, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        qApp->sendEvent( child, &localMouseEvent );

        // never eat event
        return false;

    }

    //_____________________________________________________________
    bool WindowManager::mouseMoveEvent( QObject* object, QEvent* event )
    {

        Q_UNUSED( object );

        // stop timer
        if( _dragTimer.isActive() ) _dragTimer.stop();

        // cast event and check drag distance
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>( event );
        if( !_dragInProgress )
        {

            if( _dragAboutToStart )
            {
                if( mouseEvent->globalPos() == _globalDragPoint )
                {
                    // start timer,
                    _dragAboutToStart = false;
                    if( _dragTimer.isActive() ) _dragTimer.stop();
                    _dragTimer.start( _dragDelay, this );

                } else resetDrag();

            } else if( QPoint( mouseEvent->globalPos() - _globalDragPoint ).manhattanLength() >= _dragDistance ) {

                _dragTimer.start( 0, this );

            }

            return true;

        } else if( !useWMMoveResize() ) {

            // use QWidget::move for the grabbing
            /* this works only if the sending object and the target are identical */
            QWidget* window( _target.data()->window() );
            window->move( window->pos() + mouseEvent->pos() - _dragPoint );
            return true;

        } else return false;

    }

    //_____________________________________________________________
    bool WindowManager::mouseReleaseEvent( QObject* object, QEvent* event )
    {

        Q_UNUSED( object );
        Q_UNUSED( event );
        resetDrag();
        return false;
    }

    //_____________________________________________________________
    bool WindowManager::isDragable( QWidget* widget )
    {

        // check widget
        if( !widget ) return false;

        // accepted default types
        if(
            ( qobject_cast<QDialog*>( widget ) && widget->isWindow() ) ||
            ( qobject_cast<QMainWindow*>( widget ) && widget->isWindow() ) ||
            qobject_cast<QGroupBox*>( widget ) )
        { return true; }

        // more accepted types, provided they are not dock widget titles
        if( ( qobject_cast<QMenuBar*>( widget ) ||
            qobject_cast<QTabBar*>( widget ) ||
            qobject_cast<QStatusBar*>( widget ) ||
            qobject_cast<QToolBar*>( widget ) ) &&
            !isDockWidgetTitle( widget ) )
        { return true; }

        if( widget->inherits( "KScreenSaver" ) && widget->inherits( "KCModule" ) )
        { return true; }

        if( isWhiteListed( widget ) )
        { return true; }

        // flat toolbuttons
        if( QToolButton* toolButton = qobject_cast<QToolButton*>( widget ) )
        { if( toolButton->autoRaise() ) return true; }

        // viewports
        /*
        one needs to check that
        1/ the widget parent is a scrollarea
        2/ it matches its parent viewport
        3/ the parent is not blacklisted
        */
        if( QListView* listView = qobject_cast<QListView*>( widget->parentWidget() ) )
        { if( listView->viewport() == widget && !isBlackListed( listView ) ) return true; }

        if( QTreeView* treeView = qobject_cast<QTreeView*>( widget->parentWidget() ) )
        { if( treeView->viewport() == widget && !isBlackListed( treeView ) ) return true; }

        /*
        catch labels in status bars.
        this is because of kstatusbar
        who captures buttonPress/release events
        */
        if( QLabel* label = qobject_cast<QLabel*>( widget ) )
        {
            if( label->textInteractionFlags().testFlag( Qt::TextSelectableByMouse ) ) return false;

            QWidget* parent = label->parentWidget();
            while( parent )
            {
                if( qobject_cast<QStatusBar*>( parent ) ) return true;
                parent = parent->parentWidget();
            }
        }

        return false;

    }

    //_____________________________________________________________
    bool WindowManager::isBlackListed( QWidget* widget )
    {

        // check against noAnimations propery
        QVariant propertyValue( widget->property( PropertyNames::noWindowGrab ) );
        if( propertyValue.isValid() && propertyValue.toBool() ) return true;

        // list-based blacklisted widgets
        QString appName( qApp->applicationName() );
        foreach( const ExceptionId& id, _blackList )
        {
            if( !id.appName().isEmpty() && id.appName() != appName ) continue;
            if( id.className() == QStringLiteral( "*" ) && !id.appName().isEmpty() )
            {
                // if application name matches and all classes are selected
                // disable the grabbing entirely
                setEnabled( false );
                return true;
            }
            if( widget->inherits( id.className().toLatin1().data() ) ) return true;
        }

        return false;
    }

    //_____________________________________________________________
    bool WindowManager::isWhiteListed( QWidget* widget ) const
    {

        QString appName( qApp->applicationName() );
        foreach( const ExceptionId& id, _whiteList )
        {
            if( !id.appName().isEmpty() && id.appName() != appName ) continue;
            if( widget->inherits( id.className().toLatin1().data() ) ) return true;
        }

        return false;
    }

    //_____________________________________________________________
    bool WindowManager::canDrag( QWidget* widget )
    {

        // check if enabled
        if( !enabled() ) return false;

        // assume isDragable widget is already passed
        // check some special cases where drag should not be effective

        // check mouse grabber
        if( QWidget::mouseGrabber() ) return false;

        /*
        check cursor shape.
        Assume that a changed cursor means that some action is in progress
        and should prevent the drag
        */
        if( widget->cursor().shape() != Qt::ArrowCursor ) return false;

        // accept
        return true;

    }

    //_____________________________________________________________
    bool WindowManager::canDrag( QWidget* widget, QWidget* child, const QPoint& position )
    {

        // retrieve child at given position and check cursor again
        if( child && child->cursor().shape() != Qt::ArrowCursor ) return false;

        /*
        check against children from which drag should never be enabled,
        even if mousePress/Move has been passed to the parent
        */
        if( child && (
            qobject_cast<QComboBox*>(child ) ||
            qobject_cast<QProgressBar*>( child ) ||
            qobject_cast<QScrollBar*>( child ) ) )
        { return false; }

        // tool buttons
        if( QToolButton* toolButton = qobject_cast<QToolButton*>( widget ) )
        {
            if( dragMode() == StyleConfigData::WD_MINIMAL && !qobject_cast<QToolBar*>(widget->parentWidget() ) ) return false;
            return toolButton->autoRaise() && !toolButton->isEnabled();
        }

        // check menubar
        if( QMenuBar* menuBar = qobject_cast<QMenuBar*>( widget ) )
        {

            // do not drag from menubars embedded in Mdi windows
            if( findParent<QMdiSubWindow*>( widget ) ) return false;

            // check if there is an active action
            if( menuBar->activeAction() && menuBar->activeAction()->isEnabled() ) return false;

            // check if action at position exists and is enabled
            if( QAction* action = menuBar->actionAt( position ) )
            {
                if( action->isSeparator() ) return true;
                if( action->isEnabled() ) return false;
            }

            // return true in all other cases
            return true;

        }

        /*
        in MINIMAL mode, anything that has not been already accepted
        and does not come from a toolbar is rejected
        */
        if( dragMode() == StyleConfigData::WD_MINIMAL )
        {
            if( qobject_cast<QToolBar*>( widget ) ) return true;
            else return false;
        }

        /* following checks are relevant only for WD_FULL mode */

        // tabbar. Make sure no tab is under the cursor
        if( QTabBar* tabBar = qobject_cast<QTabBar*>( widget ) )
        { return tabBar->tabAt( position ) == -1; }

        /*
        check groupboxes
        prevent drag if unchecking grouboxes
        */
        if( QGroupBox *groupBox = qobject_cast<QGroupBox*>( widget ) )
        {
            // non checkable group boxes are always ok
            if( !groupBox->isCheckable() ) return true;

            // gather options to retrieve checkbox subcontrol rect
            QStyleOptionGroupBox opt;
            opt.initFrom( groupBox );
            if( groupBox->isFlat() ) opt.features |= QStyleOptionFrameV2::Flat;
            opt.lineWidth = 1;
            opt.midLineWidth = 0;
            opt.text = groupBox->title();
            opt.textAlignment = groupBox->alignment();
            opt.subControls = (QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxCheckBox);
            if (!groupBox->title().isEmpty()) opt.subControls |= QStyle::SC_GroupBoxLabel;

            opt.state |= (groupBox->isChecked() ? QStyle::State_On : QStyle::State_Off);

            // check against groupbox checkbox
            if( groupBox->style()->subControlRect(QStyle::CC_GroupBox, &opt, QStyle::SC_GroupBoxCheckBox, groupBox ).contains( position ) )
            { return false; }

            // check against groupbox label
            if( !groupBox->title().isEmpty() && groupBox->style()->subControlRect(QStyle::CC_GroupBox, &opt, QStyle::SC_GroupBoxLabel, groupBox ).contains( position ) )
            { return false; }

            return true;

        }

        // labels
        if( QLabel* label = qobject_cast<QLabel*>( widget ) )
        { if( label->textInteractionFlags().testFlag( Qt::TextSelectableByMouse ) ) return false; }

        // abstract item views
        QAbstractItemView* itemView( NULL );
        if(
            ( itemView = qobject_cast<QListView*>( widget->parentWidget() ) ) ||
            ( itemView = qobject_cast<QTreeView*>( widget->parentWidget() ) ) )
        {
            if( widget == itemView->viewport() )
            {
                // QListView
                if( itemView->frameShape() != QFrame::NoFrame ) return false;
                else if(
                    itemView->selectionMode() != QAbstractItemView::NoSelection &&
                    itemView->selectionMode() != QAbstractItemView::SingleSelection &&
                    itemView->model() && itemView->model()->rowCount() ) return false;
                else if( itemView->model() && itemView->indexAt( position ).isValid() ) return false;
            }

        } else if( ( itemView = qobject_cast<QAbstractItemView*>( widget->parentWidget() ) ) ) {


            if( widget == itemView->viewport() )
            {
                // QAbstractItemView
                if( itemView->frameShape() != QFrame::NoFrame ) return false;
                else if( itemView->indexAt( position ).isValid() ) return false;
            }

        } else if( QGraphicsView* graphicsView =  qobject_cast<QGraphicsView*>( widget->parentWidget() ) )  {

            if( widget == graphicsView->viewport() )
            {
                // QGraphicsView
                if( graphicsView->frameShape() != QFrame::NoFrame ) return false;
                else if( graphicsView->dragMode() != QGraphicsView::NoDrag ) return false;
                else if( graphicsView->itemAt( position ) ) return false;
            }

        }

        return true;

    }

    //____________________________________________________________
    void WindowManager::resetDrag( void )
    {

        if( (!useWMMoveResize() ) && _target && _cursorOverride ) {

          qApp->restoreOverrideCursor();
          _cursorOverride = false;

        }

        _target.clear();
        if( _dragTimer.isActive() ) _dragTimer.stop();
        _dragPoint = QPoint();
        _globalDragPoint = QPoint();
        _dragAboutToStart = false;
        _dragInProgress = false;

    }

    //____________________________________________________________
    void WindowManager::startDrag( QWidget* widget, const QPoint& position )
    {

        if( !( enabled() && widget ) ) return;
        if( QWidget::mouseGrabber() ) return;

        // ungrab pointer
        if( useWMMoveResize() )
        {

            #if HAVE_X11
            // connection
            xcb_connection_t* connection( QX11Info::connection() );

            // window
            const WId window( widget->window()->winId() );

            // from bespin/virtuality
            xcb_button_release_event_t releaseEvent;
            memset(&releaseEvent, 0, sizeof(releaseEvent));

            releaseEvent.response_type = XCB_BUTTON_RELEASE;
            //     releaseEvent.sequence = ??;
            releaseEvent.event = window;
            releaseEvent.child = XCB_WINDOW_NONE;
            releaseEvent.root = QX11Info::appRootWindow();
            releaseEvent.event_x = _dragPoint.x();
            releaseEvent.event_y = _dragPoint.x();
            releaseEvent.root_x = position.x();
            releaseEvent.root_y = position.y();
            releaseEvent.detail = XCB_BUTTON_INDEX_1;
            releaseEvent.state = XCB_BUTTON_MASK_1;
            releaseEvent.time = XCB_CURRENT_TIME;
            releaseEvent.same_screen = true;
            xcb_send_event( connection, false, window, XCB_EVENT_MASK_BUTTON_RELEASE, reinterpret_cast<const char*>(&releaseEvent));

            xcb_ungrab_pointer( connection, XCB_TIME_CURRENT_TIME );

            // from QtCurve
            xcb_client_message_event_t clientMessageEvent;
            memset(&clientMessageEvent, 0, sizeof(clientMessageEvent));

            clientMessageEvent.response_type = XCB_CLIENT_MESSAGE;
            clientMessageEvent.format = 32;
            clientMessageEvent.window = window;
            clientMessageEvent.type = _moveResizeAtom;
            clientMessageEvent.data.data32[0] = position.x();
            clientMessageEvent.data.data32[1] = position.y();
            clientMessageEvent.data.data32[2] = 8; // NET::Move
            clientMessageEvent.data.data32[3] = XCB_KEY_BUT_MASK_BUTTON_1;
            xcb_send_event( connection, false, QX11Info::appRootWindow(),
                XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, reinterpret_cast<const char*>(&clientMessageEvent) );

            xcb_flush( connection );

            #else

            Q_UNUSED( position );

            #endif

        } else if( !_cursorOverride ) {

            qApp->setOverrideCursor( Qt::SizeAllCursor );
            _cursorOverride = true;

        }

        _dragInProgress = true;

        return;

    }

    //____________________________________________________________
    bool WindowManager::supportWMMoveResize( void ) const
    {

        #if HAVE_X11
        return _isX11;
        #endif

        return false;

    }

    //____________________________________________________________
    bool WindowManager::isDockWidgetTitle( const QWidget* widget ) const
    {

        if( !widget ) return false;
        if( const QDockWidget* dockWidget = qobject_cast<const QDockWidget*>( widget->parent() ) )
        {

            return widget == dockWidget->titleBarWidget();

        } else return false;

    }

    //____________________________________________________________
    bool WindowManager::AppEventFilter::eventFilter( QObject* object, QEvent* event )
    {

        if( event->type() == QEvent::MouseButtonRelease )
        {

            // stop drag timer
            if( _parent->_dragTimer.isActive() )
            { _parent->resetDrag(); }

            // unlock
            if( _parent->isLocked() )
            { _parent->setLocked( false ); }

        }

        if( !_parent->enabled() ) return false;

        /*
        if a drag is in progress, the widget will not receive any event
        we trigger on the first MouseMove or MousePress events that are received
        by any widget in the application to detect that the drag is finished
        */
        if( _parent->useWMMoveResize() && _parent->_dragInProgress && _parent->_target && ( event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress ) )
        { return appMouseEvent( object, event ); }

        return false;

    }

    //_____________________________________________________________
    bool WindowManager::AppEventFilter::appMouseEvent( QObject* object, QEvent* event )
    {

        Q_UNUSED( object );

        // store target window (see later)
        QWidget* window( _parent->_target.data()->window() );

        /*
        post some mouseRelease event to the target, in order to counter balance
        the mouse press that triggered the drag. Note that it triggers a resetDrag
        */
        QMouseEvent mouseEvent( QEvent::MouseButtonRelease, _parent->_dragPoint, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        qApp->sendEvent( _parent->_target.data(), &mouseEvent );

        if( event->type() == QEvent::MouseMove )
        {
            /*
            HACK: quickly move the main cursor out of the window and back
            this is needed to get the focus right for the window children
            the origin of this issue is unknown at the moment
            */
            const QPoint cursor = QCursor::pos();
            QCursor::setPos(window->mapToGlobal( window->rect().topRight() ) + QPoint(1, 0) );
            QCursor::setPos(cursor);

        }

        return false;

    }


}
