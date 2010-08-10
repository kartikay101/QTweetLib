/* Copyright (c) 2010, Antonie Jovanoski
 *
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Contact e-mail: Antonie Jovanoski <minimoog77_at_gmail.com>
 */

#include <QtDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QAuthenticator>
#include <QTimer>
#include "qtweetuserstream.h"

#define TWITTER_USERSTREAM_URL "https://betastream.twitter.com/2b/user.json"

QTweetUserStream::QTweetUserStream(QObject *parent) :
    QObject(parent), m_netManager(0), m_reply(0), m_backofftimer(new QTimer(this))
{
    m_backofftimer->setSingleShot(true);
    connect(m_backofftimer, SIGNAL(timeout()), this, SLOT(startFetching()));
}

void QTweetUserStream::setNetworkAccessManager(QNetworkAccessManager *netManager)
{
    m_netManager = netManager;
    connect(m_netManager, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)),
            this, SLOT(authRequired(QNetworkReply*,QAuthenticator*)));
}

QNetworkAccessManager* QTweetUserStream::networkAccessManager() const
{
    return m_netManager;
}

void QTweetUserStream::setUsername(const QString& username)
{
    m_username = username;
}

void QTweetUserStream::setPassword(const QString& password)
{
    m_password = password;
}

void QTweetUserStream::authRequired(QNetworkReply *reply, QAuthenticator *authenticator)
{
    authenticator->setUser(m_username);
    authenticator->setPassword(m_password);
}

void QTweetUserStream::replyError(QNetworkReply::NetworkError code)
{
    qDebug() << "Reply error: " << code;

    // ### TODO: determine network error codes, assumptions here

    if (code < 200) {
        //linear backoff
        if (m_backofftimer->interval() < 250) {
            m_backofftimer->setInterval(250);
        } else {
            int nextLinInterval = m_backofftimer->interval() + 250;

            if (nextLinInterval > 16000)    //cap
                nextLinInterval = 16000;

            m_backofftimer->setInterval(nextLinInterval);
        }

        m_backofftimer->start();
        return;
    }

    if (code > 200) {
        //exp. backoff
        if (m_backofftimer->interval() < 10000) {
            m_backofftimer->setInterval(10000);
        } else {
            int nextExpInterval = 2 * m_backofftimer->interval();

            if (nextExpInterval > 240000)
                nextExpInterval = 240000;

            m_backofftimer->setInterval(240000);
        }

        m_backofftimer->start();
    }
}

void QTweetUserStream::startFetching()
{
    if (m_reply != 0) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = 0;
    }

    QNetworkRequest req;
    req.setUrl(QUrl(TWITTER_USERSTREAM_URL));

    m_reply = m_netManager->get(req);
    connect(m_reply, SIGNAL(finished()), this, SLOT(replyFinished()));
    connect(m_reply, SIGNAL(readyRead()), this, SLOT(replyReadyRead()));
    connect(m_reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(replyError(QNetworkReply::NetworkError)));
}

void QTweetUserStream::replyFinished()
{
    if (!m_reply->error()) { //no error, reconnect

        //sligh delay for reconnect
        m_backofftimer->setInterval(250);
        m_backofftimer->start();
        m_reply->deleteLater();
        m_reply = 0;
    } else {    //error, delete QNetworkReply
        m_reply->deleteLater();
        m_reply = 0;
    }
}

void QTweetUserStream::replyReadyRead()
{
    QByteArray response = m_reply->readAll();

    //reset timer
    m_backofftimer->setInterval(0);

    //split to response to delimited and not delimited part
    int lastCarrReturn = response.lastIndexOf('\r');
    QByteArray rightNotDelimitedPart = response.mid(lastCarrReturn + 1);
    QByteArray leftDelimitedPart = response.left(lastCarrReturn);

    //prepend to left previous not delimited response
    leftDelimitedPart = leftDelimitedPart.prepend(m_cashedResponse);

    QList<QByteArray> elements = leftDelimitedPart.split('\r');

    for (int i = 0; i < elements.size(); ++i) {

        QByteArray test = elements.at(i);

        if (elements.at(i) != QByteArray(1, '\n'))
            emit stream(elements.at(i));
    }

    if (rightNotDelimitedPart != QByteArray(1, '\n'))
        m_cashedResponse = rightNotDelimitedPart;
    else
        m_cashedResponse.clear();
}