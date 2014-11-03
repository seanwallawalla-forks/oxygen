#ifndef oxygensizegrip_h
#define oxygensizegrip_h

//////////////////////////////////////////////////////////////////////////////
// oxygensizegrip.h
// bottom right size grip for borderless windows
// -------------------
//
// Copyright (c) 2009 Hugo Pereira Da Costa <hugo.pereira@free.fr>
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

#include "oxygenclient.h"
#include "oxygendecorationdefines.h"

#include <QWidget>
#include <QPaintEvent>
#include <QMouseEvent>

namespace Oxygen
{

    class Client;

    //* implements size grip for all widgets
    class SizeGrip: public QWidget
    {

        public:

        //* constructor
        explicit SizeGrip( Client* );

        //* constructor
        virtual ~SizeGrip( void );

        //* event filter
        virtual bool eventFilter( QObject*, QEvent* );

        public Q_SLOTS:

        //* update background color
        void activeChange( void );

        protected Q_SLOTS:

        //* embed into parent widget
        void embed( void );

        protected:

        //*@name event handlers
        //@{

        //* paint
        virtual void paintEvent( QPaintEvent* );

        //* mouse press
        virtual void mousePressEvent( QMouseEvent* );

        //@}

        //* update position
        void updatePosition( void );

        private:

        //* grip size
        enum {
            Offset = 0,
            GripSize = 14
        };

        // oxygen client
        WeakPointer<Client> _client;

    };


}

#endif
