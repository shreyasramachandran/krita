/*
 *  Copyright (c) 2002 Patrick Julien <freak@codepimps.org>
 *  Copyright (c) 2007 Sven Langkamp <sven.langkamp@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "kis_image_layer_remove_command_impl.h"
#include "kis_image.h"

#include <klocalizedstring.h>
#include "kis_layer.h"
#include "kis_clone_layer.h"
#include "kis_paint_layer.h"

struct Q_DECL_HIDDEN KisImageLayerRemoveCommandImpl::Private {
    Private(KisImageLayerRemoveCommandImpl *_q) : q(_q) {}

    KisImageLayerRemoveCommandImpl *q;

    KisNodeSP node;
    KisNodeSP prevParent;
    KisNodeSP prevAbove;

    QList<KisCloneLayerSP> clonesList;
    QList<KisLayerSP> reincarnatedNodes;

    void restoreClones();
    void processClones(KisNodeSP node);
    void moveChildren(KisNodeSP src, KisNodeSP dst);
    void moveClones(KisLayerSP src, KisLayerSP dst);
};

KisImageLayerRemoveCommandImpl::KisImageLayerRemoveCommandImpl(KisImageWSP image, KisNodeSP node, KUndo2Command *parent)
    : KisImageCommand(kundo2_i18n("Remove Layer"), image, parent),
      m_d(new Private(this))
{
    m_d->node = node;
    m_d->prevParent = node->parent();
    m_d->prevAbove = node->prevSibling();
}

KisImageLayerRemoveCommandImpl::~KisImageLayerRemoveCommandImpl()
{
    delete m_d;
}

void KisImageLayerRemoveCommandImpl::redo()
{
    KisImageSP image = m_image.toStrongRef();
    if (!image) {
        return;
    }
    m_d->processClones(m_d->node);
    image->removeNode(m_d->node);
}

void KisImageLayerRemoveCommandImpl::undo()
{
    KisImageSP image = m_image.toStrongRef();
    if (!image) {
        return;
    }
    image->addNode(m_d->node, m_d->prevParent, m_d->prevAbove);
    m_d->restoreClones();
}

void KisImageLayerRemoveCommandImpl::Private::restoreClones()
{
    Q_ASSERT(reincarnatedNodes.size() == clonesList.size());
    KisImageSP image = q->m_image.toStrongRef();
    if (!image) {
        return;
    }
    for (int i = 0; i < reincarnatedNodes.size(); i++) {
        KisCloneLayerSP clone = clonesList[i];
        KisLayerSP newNode = reincarnatedNodes[i];

        image->addNode(clone, newNode->parent(), newNode);
        moveChildren(newNode, clone);
        moveClones(newNode, clone);
        image->removeNode(newNode);
    }
}

void KisImageLayerRemoveCommandImpl::Private::processClones(KisNodeSP node)
{
    KisLayerSP layer(qobject_cast<KisLayer*>(node.data()));
    if(!layer || !layer->hasClones()) return;

    if(reincarnatedNodes.isEmpty()) {
        /**
         * Initialize the list of reincarnates nodes
         */
        Q_FOREACH (KisCloneLayerWSP _clone, layer->registeredClones()) {
            KisCloneLayerSP clone = _clone;
            Q_ASSERT(clone);

            clonesList.append(clone);
            reincarnatedNodes.append(clone->reincarnateAsPaintLayer());
        }
    }

    KisImageSP image = q->m_image.toStrongRef();
    if (!image) {
        return;
    }

    /**
     * Move the children and transitive clones to the
     * reincarnated nodes
     */
    for (int i = 0; i < reincarnatedNodes.size(); i++) {
        KisCloneLayerSP clone = clonesList[i];
        KisLayerSP newNode = reincarnatedNodes[i];

        image->addNode(newNode, clone->parent(), clone);
        moveChildren(clone, newNode);
        moveClones(clone, newNode);
        image->removeNode(clone);
    }
}

void KisImageLayerRemoveCommandImpl::Private::moveChildren(KisNodeSP src, KisNodeSP dst)
{
    KisImageSP image = q->m_image.toStrongRef();
    if (!image) {
        return;
    }
    KisNodeSP child = src->firstChild();
    while(child) {
        image->moveNode(child, dst, dst->lastChild());
        child = child->nextSibling();
    }
}

void KisImageLayerRemoveCommandImpl::Private::moveClones(KisLayerSP src, KisLayerSP dst)
{
    Q_FOREACH (KisCloneLayerWSP _clone, src->registeredClones()) {
        KisCloneLayerSP clone = _clone;
        Q_ASSERT(clone);

        clone->setCopyFrom(dst);
    }
}
