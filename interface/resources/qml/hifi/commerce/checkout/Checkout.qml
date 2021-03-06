//
//  Checkout.qml
//  qml/hifi/commerce/checkout
//
//  Checkout
//
//  Created by Zach Fox on 2017-08-25
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import Hifi 1.0 as Hifi
import QtQuick 2.5
import QtQuick.Controls 1.4
import "../../../styles-uit"
import "../../../controls-uit" as HifiControlsUit
import "../../../controls" as HifiControls
import "../wallet" as HifiWallet
import "../common" as HifiCommerceCommon

// references XXX from root context

Rectangle {
    HifiConstants { id: hifi; }

    id: root;
    objectName: "checkout"
    property string activeView: "initialize";
    property bool ownershipStatusReceived: false;
    property bool balanceReceived: false;
    property bool availableUpdatesReceived: false;
    property string baseItemName: "";
    property string itemName;
    property string itemId;
    property string itemHref;
    property string itemAuthor;
    property int itemEdition: -1;
    property string certificateId;
    property double balanceAfterPurchase;
    property bool alreadyOwned: false;
    property int itemPrice: -1;
    property bool isCertified;
    property string itemType: "unknown";
    property var itemTypesArray: ["entity", "wearable", "contentSet", "app", "avatar", "unknown"];
    property var itemTypesText: ["entity", "wearable", "content set", "app", "avatar", "item"];
    property var buttonTextNormal: ["REZ", "WEAR", "REPLACE CONTENT SET", "INSTALL", "WEAR", "REZ"];
    property var buttonTextClicked: ["REZZED!", "WORN!", "CONTENT SET REPLACED!", "INSTALLED!", "AVATAR CHANGED!", "REZZED!"]
    property var buttonGlyph: [hifi.glyphs.wand, hifi.glyphs.hat, hifi.glyphs.globe, hifi.glyphs.install, hifi.glyphs.avatar, hifi.glyphs.wand];
    property bool shouldBuyWithControlledFailure: false;
    property bool debugCheckoutSuccess: false;
    property bool canRezCertifiedItems: Entities.canRezCertified() || Entities.canRezTmpCertified();
    property string referrer;
    property bool isInstalled;
    property bool isUpdating;
    property string baseAppURL;
    // Style
    color: hifi.colors.white;
    Connections {
        target: Commerce;

        onWalletStatusResult: {
            if (walletStatus === 0) {
                if (root.activeView !== "needsLogIn") {
                    root.activeView = "needsLogIn";
                }
            } else if ((walletStatus === 1) || (walletStatus === 2) || (walletStatus === 3)) {
                if (root.activeView !== "notSetUp") {
                    root.activeView = "notSetUp";
                    notSetUpTimer.start();
                }
            } else if (walletStatus === 4) {
                if (root.activeView !== "passphraseModal") {
                    root.activeView = "passphraseModal";
                    UserActivityLogger.commercePassphraseEntry("marketplace checkout");
                }
            } else if (walletStatus === 5) {
                authSuccessStep();
            } else {
                console.log("ERROR in Checkout.qml: Unknown wallet status: " + walletStatus);
            }
        }

        onLoginStatusResult: {
            if (!isLoggedIn && root.activeView !== "needsLogIn") {
                root.activeView = "needsLogIn";
            } else {
                Commerce.getWalletStatus();
            }
        }

        onBuyResult: {
            if (result.status !== 'success') {
                failureErrorText.text = result.data.message;
                root.activeView = "checkoutFailure";
                UserActivityLogger.commercePurchaseFailure(root.itemId, root.itemAuthor, root.itemPrice, !root.alreadyOwned, result.data.message);
            } else {
                root.certificateId = result.data.certificate_id;
                root.itemHref = result.data.download_url;
                root.activeView = "checkoutSuccess";
                UserActivityLogger.commercePurchaseSuccess(root.itemId, root.itemAuthor, root.itemPrice, !root.alreadyOwned);
            }
        }

        onBalanceResult: {
            if (result.status !== 'success') {
                console.log("Failed to get balance", result.data.message);
            } else {
                root.balanceAfterPurchase = result.data.balance - root.itemPrice;
                root.balanceReceived = true;
                root.refreshBuyUI();
            }
        }

        onAlreadyOwnedResult: {
            if (result.status !== 'success') {
                console.log("Failed to get Already Owned status", result.data.message);
            } else {
                if (result.data.marketplace_item_id === root.itemId) {
                    root.alreadyOwned = result.data.already_owned;
                } else {
                    console.log("WARNING - Received 'Already Owned' status about different Marketplace ID!");
                    root.alreadyOwned = false;
                }
                root.ownershipStatusReceived = true;
                root.refreshBuyUI();
            }
        }

        onAppInstalled: {
            if (appHref === root.itemHref) {
                root.isInstalled = true;
            }
        }

        onAvailableUpdatesResult: {
            if (result.status !== 'success') {
                console.log("Failed to get Available Updates", result.data.message);
            } else {
                for (var i = 0; i < result.data.updates.length; i++) {
                    // If the ItemID of the item we're looking at matches EITHER the ID of a "base" item
                    // OR the ID of an "updated" item, we're updating.
                    if (root.itemId === result.data.updates[i].item_id ||
                        root.itemId === result.data.updates[i].updated_item_id) {
                        if (root.itemEdition !== -1 && root.itemEdition !== parseInt(result.data.updates[i].edition_number)) {
                            continue;
                        }
                        root.isUpdating = true;
                        root.baseItemName = result.data.updates[i].base_item_title;
                        // This CertID is the one corresponding to the base item CertID that the user already owns
                        root.certificateId = result.data.updates[i].certificate_id;
                        if (root.itemType === "app") {
                            root.baseAppURL = result.data.updates[i].item_download_url;
                        }
                        break;
                    }
                }
                root.availableUpdatesReceived = true;
                refreshBuyUI();
            }
        }

        onUpdateItemResult: {
            if (result.status !== 'success') {
                failureErrorText.text = result.message;
                root.activeView = "checkoutFailure";
            } else {
                root.itemHref = result.data.download_url;
                root.activeView = "checkoutSuccess";
            }
        }
    }

    onItemIdChanged: {
        root.ownershipStatusReceived = false;
        Commerce.alreadyOwned(root.itemId);
        root.availableUpdatesReceived = false;
        Commerce.getAvailableUpdates(root.itemId);
        itemPreviewImage.source = "https://hifi-metaverse.s3-us-west-1.amazonaws.com/marketplace/previews/" + itemId + "/thumbnail/hifi-mp-" + itemId + ".jpg";
    }

    onItemTypeChanged: {
        if (root.itemType === "entity" || root.itemType === "wearable" ||
            root.itemType === "contentSet" || root.itemType === "avatar" || root.itemType === "app") {
            root.isCertified = true;
        } else {
            root.isCertified = false;
        }
    }

    onItemPriceChanged: {
        root.balanceReceived = false;
        Commerce.balance();
    }

    Timer {
        id: notSetUpTimer;
        interval: 200;
        onTriggered: {
            sendToScript({method: 'checkout_walletNotSetUp', itemId: itemId, referrer: referrer});
        }
    }

    HifiCommerceCommon.CommerceLightbox {
        id: lightboxPopup;
        visible: false;
        anchors.fill: parent;

        Connections {
            onSendToParent: {
                sendToScript(msg);
            }
        }
    }

    //
    // TITLE BAR START
    //
    HifiCommerceCommon.EmulatedMarketplaceHeader {
        id: titleBarContainer;
        z: 998;
        visible: !needsLogIn.visible;
        // Size
        width: parent.width;
        height: 70;
        // Anchors
        anchors.left: parent.left;
        anchors.top: parent.top;

        Connections {
            onSendToParent: {
                if (msg.method === 'needsLogIn' && root.activeView !== "needsLogIn") {
                    root.activeView = "needsLogIn";
                } else if (msg.method === 'showSecurityPicLightbox') {
                    lightboxPopup.titleText = "Your Security Pic";
                    lightboxPopup.bodyImageSource = msg.securityImageSource;
                    lightboxPopup.bodyText = lightboxPopup.securityPicBodyText;
                    lightboxPopup.button1text = "CLOSE";
                    lightboxPopup.button1method = function() {
                        lightboxPopup.visible = false;
                    }
                    lightboxPopup.button2text = "GO TO WALLET";
                    lightboxPopup.button2method = function() {
                        lightboxPopup.visible = false;
                        sendToScript({method: 'checkout_openWallet'});
                    };
                    lightboxPopup.visible = true;
                } else {
                    sendToScript(msg);
                }
            }
        }
    }
    MouseArea {
        enabled: titleBarContainer.usernameDropdownVisible;
        anchors.fill: parent;
        onClicked: {
            titleBarContainer.usernameDropdownVisible = false;
        }
    }
    //
    // TITLE BAR END
    //

    Rectangle {
        id: initialize;
        visible: root.activeView === "initialize";
        anchors.top: titleBarContainer.bottom;
        anchors.bottom: parent.top;
        anchors.left: parent.left;
        anchors.right: parent.right;
        color: hifi.colors.white;

        Component.onCompleted: {
            ownershipStatusReceived = false;
            balanceReceived = false;
            availableUpdatesReceived = false;
            Commerce.getWalletStatus();
        }
    }

    HifiWallet.NeedsLogIn {
        id: needsLogIn;
        visible: root.activeView === "needsLogIn";
        anchors.top: parent.top;
        anchors.bottom: parent.bottom;
        anchors.left: parent.left;
        anchors.right: parent.right;

        Connections {
            onSendSignalToWallet: {
                sendToScript(msg);
            }
        }
    }
    Connections {
        target: GlobalServices
        onMyUsernameChanged: {
            Commerce.getLoginStatus();
        }
    }

    HifiWallet.PassphraseModal {
        id: passphraseModal;
        visible: root.activeView === "passphraseModal";
        anchors.fill: parent;
        titleBarText: "Checkout";
        titleBarIcon: hifi.glyphs.wallet;

        Connections {
            onSendSignalToParent: {
                if (msg.method === "authSuccess") {
                    authSuccessStep();
                } else {
                    sendToScript(msg);
                }
            }
        }
    }    

    HifiCommerceCommon.FirstUseTutorial {
        id: firstUseTutorial;
        z: 999;
        visible: root.activeView === "firstUseTutorial";
        anchors.fill: parent;

        Connections {
            onSendSignalToParent: {
                switch (message.method) {
                    case 'tutorial_skipClicked':
                    case 'tutorial_finished':
                        Settings.setValue("isFirstUseOfPurchases", false);
                        root.activeView = "checkoutSuccess";
                    break;
                }
            }
        }
    }

    //
    // CHECKOUT CONTENTS START
    //
    Item {
        id: checkoutContents;
        visible: root.activeView === "checkoutMain";
        anchors.top: titleBarContainer.bottom;
        anchors.bottom: parent.bottom;
        anchors.left: parent.left;
        anchors.right: parent.right;

        Rectangle {
            id: loading;
            z: 997;
            visible: !root.ownershipStatusReceived || !root.balanceReceived || !root.availableUpdatesReceived;
            anchors.fill: parent;
            color: hifi.colors.white;

            // This object is always used in a popup.
            // This MouseArea is used to prevent a user from being
            //     able to click on a button/mouseArea underneath the popup/section.
            MouseArea {
                anchors.fill: parent;
                hoverEnabled: true;
                propagateComposedEvents: false;
            }
                
            AnimatedImage {
                id: loadingImage;
                source: "../common/images/loader-blue.gif"
                width: 74;
                height: width;
                anchors.verticalCenter: parent.verticalCenter;
                anchors.horizontalCenter: parent.horizontalCenter;
            }
        }

        RalewayRegular {
            id: confirmPurchaseText;
            anchors.top: parent.top;
            anchors.topMargin: 30;
            anchors.left: parent.left;
            anchors.leftMargin: 16;
            width: paintedWidth;
            height: paintedHeight;
            text: "Review Purchase:";
            color: hifi.colors.black;
            size: 28;
        }
        
        HifiControlsUit.Separator {
            id: separator;
            colorScheme: 1;
            anchors.left: parent.left;
            anchors.right: parent.right;
            anchors.top: confirmPurchaseText.bottom;
            anchors.topMargin: 16;
        }

        Item {
            id: itemContainer;
            anchors.top: separator.bottom;
            anchors.topMargin: 24;
            anchors.left: parent.left;
            anchors.leftMargin: 16;
            anchors.right: parent.right;
            anchors.rightMargin: 16;
            height: 120;

            Image {
                id: itemPreviewImage;
                anchors.left: parent.left;
                anchors.top: parent.top;
                anchors.bottom: parent.bottom;
                width: height;
                fillMode: Image.PreserveAspectCrop;
            }

            RalewaySemiBold {
                id: itemNameText;
                text: root.itemName;
                // Text size
                size: 26;
                // Anchors
                anchors.top: parent.top;
                anchors.left: itemPreviewImage.right;
                anchors.leftMargin: 12;
                anchors.right: itemPriceContainer.left;
                anchors.rightMargin: 8;
                height: 30;
                // Style
                color: hifi.colors.blueAccent;
                elide: Text.ElideRight;
                // Alignment
                horizontalAlignment: Text.AlignLeft;
                verticalAlignment: Text.AlignTop;
            }

            // "Item Price" container
            Item {
                id: itemPriceContainer;
                // Anchors
                anchors.top: parent.top;
                anchors.right: parent.right;
                height: 30;
                width: itemPriceTextLabel.width + itemPriceText.width + 20;

                // "HFC" balance label
                HiFiGlyphs {
                    id: itemPriceTextLabel;
                    visible: !(root.isUpdating && root.itemEdition > 0);
                    text: hifi.glyphs.hfc;
                    // Size
                    size: 30;
                    // Anchors
                    anchors.right: itemPriceText.left;
                    anchors.rightMargin: 4;
                    anchors.top: parent.top;
                    anchors.topMargin: 0;
                    width: paintedWidth;
                    height: paintedHeight;
                    // Style
                    color: hifi.colors.blueAccent;
                }
                FiraSansSemiBold {
                    id: itemPriceText;
                    text: (root.isUpdating && root.itemEdition > 0) ? "FREE\nUPDATE" : ((root.itemPrice === -1) ? "--" : root.itemPrice);
                    // Text size
                    size: (root.isUpdating && root.itemEdition > 0) ? 20 : 26;
                    // Anchors
                    anchors.top: parent.top;
                    anchors.right: parent.right;
                    anchors.rightMargin: 16;
                    width: paintedWidth;
                    height: paintedHeight;
                    // Style
                    color: hifi.colors.blueAccent;
                }
            }
        }
        
        HifiControlsUit.Separator {
            id: separator2;
            colorScheme: 1;
            anchors.left: parent.left;
            anchors.right: parent.right;
            anchors.top: itemContainer.bottom;
            anchors.topMargin: itemContainer.anchors.topMargin;
        }


        //
        // ACTION BUTTONS AND TEXT START
        //
        Item {
            id: checkoutActionButtonsContainer;
            // Size
            width: root.width;
            // Anchors
            anchors.top: separator2.bottom;
            anchors.topMargin: 0;
            anchors.left: parent.left;
            anchors.leftMargin: 16;
            anchors.right: parent.right;
            anchors.rightMargin: 16;
            anchors.bottom: parent.bottom;
            anchors.bottomMargin: 8;

            Rectangle {
                id: buyTextContainer;
                visible: buyText.text !== "";
                anchors.top: parent.top;
                anchors.topMargin: 10;
                anchors.left: parent.left;
                anchors.right: parent.right;
                height: buyText.height + 30;
                radius: 4;
                border.width: 2;

                HiFiGlyphs {
                    id: buyGlyph;
                    // Size
                    size: 46;
                    // Anchors
                    anchors.left: parent.left;
                    anchors.leftMargin: 4;
                    anchors.top: parent.top;
                    anchors.topMargin: 8;
                    anchors.bottom: parent.bottom;
                    width: paintedWidth;
                    // Style
                    color: hifi.colors.baseGray;
                    // Alignment
                    horizontalAlignment: Text.AlignHCenter;
                    verticalAlignment: Text.AlignTop;
                }

                RalewayRegular {
                    id: buyText;
                    // Text size
                    size: 18;
                    // Anchors
                    anchors.left: buyGlyph.right;
                    anchors.leftMargin: 8;
                    anchors.right: parent.right;
                    anchors.rightMargin: 12;
                    anchors.verticalCenter: parent.verticalCenter;
                    height: paintedHeight;
                    // Style
                    color: hifi.colors.black;
                    wrapMode: Text.WordWrap;
                    // Alignment
                    horizontalAlignment: Text.AlignLeft;
                    verticalAlignment: Text.AlignVCenter;
                }
            }

            // "View in My Purchases" button
            HifiControlsUit.Button {
                id: viewInMyPurchasesButton;
                visible: false;
                color: hifi.buttons.blue;
                colorScheme: hifi.colorSchemes.light;
                anchors.top: buyTextContainer.visible ? buyTextContainer.bottom : checkoutActionButtonsContainer.top;
                anchors.topMargin: 10;
                height: 50;
                anchors.left: parent.left;
                anchors.right: parent.right;
                text: root.isUpdating ? "UPDATE TO THIS ITEM FOR FREE" : "VIEW THIS ITEM IN MY PURCHASES";
                onClicked: {
                    if (root.isUpdating) {
                        sendToScript({method: 'checkout_goToPurchases', filterText: root.baseItemName});
                    } else {
                        sendToScript({method: 'checkout_goToPurchases', filterText: root.itemName});
                    }
                }
            }

            // "Buy" button
            HifiControlsUit.Button {
                id: buyButton;
                visible: !((root.itemType === "avatar" || root.itemType === "app") && viewInMyPurchasesButton.visible)
                enabled: (root.balanceAfterPurchase >= 0 && ownershipStatusReceived && balanceReceived && availableUpdatesReceived) || (!root.isCertified) || root.isUpdating;
                color: viewInMyPurchasesButton.visible ? hifi.buttons.white : hifi.buttons.blue;
                colorScheme: hifi.colorSchemes.light;
                anchors.top: viewInMyPurchasesButton.visible ? viewInMyPurchasesButton.bottom :
                    (buyTextContainer.visible ? buyTextContainer.bottom : checkoutActionButtonsContainer.top);
                anchors.topMargin: 10;
                height: 50;
                anchors.left: parent.left;
                anchors.right: parent.right;
                text: (root.isUpdating && root.itemEdition > 0) ? "CONFIRM UPDATE" : (((root.isCertified) ? ((ownershipStatusReceived && balanceReceived && availableUpdatesReceived) ?
                    ((viewInMyPurchasesButton.visible && !root.isUpdating) ? "Buy It Again" : "Confirm Purchase") : "--") : "Get Item"));
                onClicked: {
                    if (root.isUpdating && root.itemEdition > 0) {
                        // If we're updating an app, the existing app needs to be uninstalled.
                        // This call will fail/return `false` if the app isn't installed, but that's OK.
                        if (root.itemType === "app") {
                            Commerce.uninstallApp(root.baseAppURL);
                        }
                        buyButton.enabled = false;
                        loading.visible = true;
                        Commerce.updateItem(root.certificateId);
                    } else if (root.isCertified) {
                        if (!root.shouldBuyWithControlledFailure) {
                            if (root.itemType === "contentSet" && !Entities.canReplaceContent()) {
                                lightboxPopup.titleText = "Purchase Content Set";
                                lightboxPopup.bodyText = "You will not be able to replace this domain's content with <b>" + root.itemName +
                                    " </b>until the server owner gives you 'Replace Content' permissions.<br><br>Are you sure you want to purchase this content set?";
                                lightboxPopup.button1text = "CANCEL";
                                lightboxPopup.button1method = function() {
                                    lightboxPopup.visible = false;
                                }
                                lightboxPopup.button2text = "CONFIRM";
                                lightboxPopup.button2method = function() {
                                    Commerce.buy(root.itemId, root.itemPrice);
                                    lightboxPopup.visible = false;
                                    buyButton.enabled = false;
                                    loading.visible = true;
                                };
                                lightboxPopup.visible = true;
                            } else {
                                buyButton.enabled = false;
                                loading.visible = true;
                                Commerce.buy(root.itemId, root.itemPrice);
                            }
                        } else {
                            buyButton.enabled = false;
                            loading.visible = true;
                            Commerce.buy(root.itemId, root.itemPrice, true);
                        }
                    } else {
                        if (urlHandler.canHandleUrl(itemHref)) {
                            urlHandler.handleUrl(itemHref);
                        }
                    }
                }
            }

            // "Cancel" button
            HifiControlsUit.Button {
                id: cancelPurchaseButton;
                color: hifi.buttons.noneBorderlessGray;
                colorScheme: hifi.colorSchemes.light;
                anchors.top: buyButton.visible ? buyButton.bottom : viewInMyPurchasesButton.bottom;
                anchors.topMargin: 10;
                height: 50;
                anchors.left: parent.left;
                anchors.right: parent.right;
                text: "Cancel"
                onClicked: {
                    sendToScript({method: 'checkout_cancelClicked', params: itemId});
                }
            }
        }
        //
        // ACTION BUTTONS END
        //
    }
    //
    // CHECKOUT CONTENTS END
    //

    //
    // CHECKOUT SUCCESS START
    //
    Item {
        id: checkoutSuccess;
        visible: root.activeView === "checkoutSuccess";
        anchors.top: titleBarContainer.bottom;
        anchors.bottom: root.bottom;
        anchors.left: parent.left;
        anchors.leftMargin: 20;
        anchors.right: parent.right;
        anchors.rightMargin: 20;

        RalewayRegular {
            id: completeText;
            anchors.top: parent.top;
            anchors.topMargin: 18;
            anchors.left: parent.left;
            width: paintedWidth;
            height: paintedHeight;
            text: "Thank you for your order!";
            color: hifi.colors.baseGray;
            size: 36;
        }

        RalewaySemiBold {
            id: completeText2;
            text: "The " + (root.itemTypesText)[itemTypesArray.indexOf(root.itemType)] +
                ' <font color="' + hifi.colors.blueAccent + '"><a href="#">' + root.itemName + '</a></font>' +
                " has been added to your Purchases and a receipt will appear in your Wallet's transaction history.";
            // Text size
            size: 18;
            // Anchors
            anchors.top: completeText.bottom;
            anchors.topMargin: 15;
            height: paintedHeight;
            anchors.left: parent.left;
            anchors.right: parent.right;
            // Style
            color: hifi.colors.black;
            wrapMode: Text.WordWrap;
            // Alignment
            horizontalAlignment: Text.AlignLeft;
            verticalAlignment: Text.AlignVCenter;
            onLinkActivated: {
                sendToScript({method: 'checkout_itemLinkClicked', itemId: itemId});
            }
        }
        
        Rectangle {
            id: rezzedNotifContainer;
            z: 997;
            visible: false;
            color: hifi.colors.blueHighlight;
            anchors.fill: rezNowButton;
            radius: 5;
            MouseArea {
                anchors.fill: parent;
                propagateComposedEvents: false;
                hoverEnabled: true;
            }

            RalewayBold {
                anchors.fill: parent;
                text: (root.buttonTextClicked)[itemTypesArray.indexOf(root.itemType)];
                size: 18;
                color: hifi.colors.white;
                verticalAlignment: Text.AlignVCenter;
                horizontalAlignment: Text.AlignHCenter;
            }

            Timer {
                id: rezzedNotifContainerTimer;
                interval: 2000;
                onTriggered: rezzedNotifContainer.visible = false
            }
        }
        // "Rez" button
        HifiControlsUit.Button {
            id: rezNowButton;
            enabled: (root.itemType === "entity" && root.canRezCertifiedItems) ||
                (root.itemType === "contentSet" && Entities.canReplaceContent()) ||
                root.itemType === "wearable" || root.itemType === "avatar" || root.itemType === "app";
            buttonGlyph: (root.buttonGlyph)[itemTypesArray.indexOf(root.itemType)];
            color: hifi.buttons.red;
            colorScheme: hifi.colorSchemes.light;
            anchors.top: completeText2.bottom;
            anchors.topMargin: 27;
            height: 50;
            anchors.left: parent.left;
            anchors.right: parent.right;
            text: root.itemType === "app" && root.isInstalled ? "OPEN APP" : (root.buttonTextNormal)[itemTypesArray.indexOf(root.itemType)];
            onClicked: {
                if (root.itemType === "contentSet") {
                    lightboxPopup.titleText = "Replace Content";
                    lightboxPopup.bodyText = "Rezzing this content set will replace the existing environment and all of the items in this domain. " +
                        "If you want to save the state of the content in this domain, create a backup before proceeding.<br><br>" +
                        "For more information about backing up and restoring content, " +
                        "<a href='https://docs.highfidelity.com/create-and-explore/start-working-in-your-sandbox/restoring-sandbox-content'>" +
                        "click here to open info on your desktop browser.";
                    lightboxPopup.button1text = "CANCEL";
                    lightboxPopup.button1method = function() {
                        lightboxPopup.visible = false;
                    }
                    lightboxPopup.button2text = "CONFIRM";
                    lightboxPopup.button2method = function() {
                        Commerce.replaceContentSet(root.itemHref, root.certificateId);
                        lightboxPopup.visible = false;
                        rezzedNotifContainer.visible = true;
                        rezzedNotifContainerTimer.start();
                        UserActivityLogger.commerceEntityRezzed(root.itemId, 'checkout', root.itemType);
                    };
                    lightboxPopup.visible = true;
                } else if (root.itemType === "avatar") {
                    lightboxPopup.titleText = "Change Avatar";
                    lightboxPopup.bodyText = "This will change your current avatar to " + root.itemName + " while retaining your wearables.";
                    lightboxPopup.button1text = "CANCEL";
                    lightboxPopup.button1method = function() {
                        lightboxPopup.visible = false;
                    }
                    lightboxPopup.button2text = "CONFIRM";
                    lightboxPopup.button2method = function() {
                        MyAvatar.useFullAvatarURL(root.itemHref);
                        lightboxPopup.visible = false;
                    };
                    lightboxPopup.visible = true;
                } else if (root.itemType === "app") {
                    if (root.isInstalled) {
                        Commerce.openApp(root.itemHref);
                    } else {
                        Commerce.installApp(root.itemHref);
                    }
                } else {
                    sendToScript({method: 'checkout_rezClicked', itemHref: root.itemHref, itemType: root.itemType});
                    rezzedNotifContainer.visible = true;
                    rezzedNotifContainerTimer.start();
                    UserActivityLogger.commerceEntityRezzed(root.itemId, "checkout", root.itemType);
                }
            }
        }
        RalewaySemiBold {
            id: noPermissionText;
            visible: !root.canRezCertifiedItems && root.itemType === "entity";
            text: '<font color="' + hifi.colors.redAccent + '"><a href="#">You do not have Certified Rez permissions in this domain.</a></font>'
            // Text size
            size: 16;
            // Anchors
            anchors.top: rezNowButton.bottom;
            anchors.topMargin: 4;
            height: paintedHeight;
            anchors.left: parent.left;
            anchors.right: parent.right;
            // Style
            color: hifi.colors.redAccent;
            wrapMode: Text.WordWrap;
            // Alignment
            horizontalAlignment: Text.AlignHCenter;
            verticalAlignment: Text.AlignVCenter;
            onLinkActivated: {
                lightboxPopup.titleText = "Rez Permission Required";
                lightboxPopup.bodyText = "You don't have permission to rez certified items in this domain.<br><br>" +
                    "Use the <b>GOTO app</b> to visit another domain or <b>go to your own sandbox.</b>";
                lightboxPopup.button1text = "CLOSE";
                lightboxPopup.button1method = function() {
                    lightboxPopup.visible = false;
                }
                lightboxPopup.button2text = "OPEN GOTO";
                lightboxPopup.button2method = function() {
                    sendToScript({method: 'purchases_openGoTo'});
                    lightboxPopup.visible = false;
                };
                lightboxPopup.visible = true;
            }
        }
        RalewaySemiBold {
            id: explainRezText;
            visible: root.itemType === "entity";
            text: '<font color="' + hifi.colors.redAccent + '"><a href="#">What does "Rez" mean?</a></font>'
            // Text size
            size: 16;
            // Anchors
            anchors.top: noPermissionText.visible ? noPermissionText.bottom : rezNowButton.bottom;
            anchors.topMargin: 6;
            height: paintedHeight;
            anchors.left: parent.left;
            anchors.right: parent.right;
            // Style
            color: hifi.colors.redAccent;
            wrapMode: Text.WordWrap;
            // Alignment
            horizontalAlignment: Text.AlignHCenter;
            verticalAlignment: Text.AlignVCenter;
            onLinkActivated: {
                root.activeView = "firstUseTutorial";
            }
        }

        RalewaySemiBold {
            id: myPurchasesLink;
            text: '<font color="' + hifi.colors.primaryHighlight + '"><a href="#">View this item in My Purchases</a></font>';
            // Text size
            size: 18;
            // Anchors
            anchors.top: explainRezText.visible ? explainRezText.bottom : (noPermissionText.visible ? noPermissionText.bottom : rezNowButton.bottom);
            anchors.topMargin: 40;
            height: paintedHeight;
            anchors.left: parent.left;
            anchors.right: parent.right;
            // Style
            color: hifi.colors.black;
            wrapMode: Text.WordWrap;
            // Alignment
            horizontalAlignment: Text.AlignLeft;
            verticalAlignment: Text.AlignVCenter;
            onLinkActivated: {
                sendToScript({method: 'checkout_goToPurchases'});
            }
        }

        RalewaySemiBold {
            id: walletLink;
            text: '<font color="' + hifi.colors.primaryHighlight + '"><a href="#">View receipt in Wallet</a></font>';
            // Text size
            size: 18;
            // Anchors
            anchors.top: myPurchasesLink.bottom;
            anchors.topMargin: 16;
            height: paintedHeight;
            anchors.left: parent.left;
            anchors.right: parent.right;
            // Style
            color: hifi.colors.black;
            wrapMode: Text.WordWrap;
            // Alignment
            horizontalAlignment: Text.AlignLeft;
            verticalAlignment: Text.AlignVCenter;
            onLinkActivated: {
                sendToScript({method: 'purchases_openWallet'});
            }
        }

        RalewayRegular {
            id: pendingText;
            text: 'Your item is marked "pending" while your purchase is being confirmed. ' +
            '<b><font color="' + hifi.colors.primaryHighlight + '"><a href="#">Learn More</a></font></b>';
            // Text size
            size: 18;
            // Anchors
            anchors.top: walletLink.bottom;
            anchors.topMargin: 32;
            height: paintedHeight;
            anchors.left: parent.left;
            anchors.right: parent.right;
            // Style
            color: hifi.colors.black;
            wrapMode: Text.WordWrap;
            // Alignment
            horizontalAlignment: Text.AlignLeft;
            verticalAlignment: Text.AlignVCenter;
            onLinkActivated: {
                lightboxPopup.titleText = "Purchase Confirmations";
                lightboxPopup.bodyText = 'Your item is marked "pending" while your purchase is being confirmed.<br><br>' +
                'Confirmations usually take about 90 seconds.';
                lightboxPopup.button1text = "CLOSE";
                lightboxPopup.button1method = function() {
                    lightboxPopup.visible = false;
                }
                lightboxPopup.visible = true;
            }
        }

        // "Continue Shopping" button
        HifiControlsUit.Button {
            id: continueShoppingButton;
            color: hifi.buttons.noneBorderlessGray;
            colorScheme: hifi.colorSchemes.light;
            anchors.bottom: parent.bottom;
            anchors.bottomMargin: 54;
            anchors.right: parent.right;
            width: 193;
            height: 44;
            text: "Continue Shopping";
            onClicked: {
                sendToScript({method: 'checkout_continueShopping', itemId: itemId});
            }
        }
    }
    //
    // CHECKOUT SUCCESS END
    //

    //
    // CHECKOUT FAILURE START
    //
    Item {
        id: checkoutFailure;
        visible: root.activeView === "checkoutFailure";
        anchors.top: titleBarContainer.bottom;
        anchors.bottom: root.bottom;
        anchors.left: parent.left;
        anchors.leftMargin: 16;
        anchors.right: parent.right;
        anchors.rightMargin: 16;

        RalewayRegular {
            id: failureHeaderText;
            text: "<b>Purchase Failed.</b><br>Your Purchases and HFC balance haven't changed.";
            // Text size
            size: 24;
            // Anchors
            anchors.top: parent.top;
            anchors.topMargin: 40;
            height: paintedHeight;
            anchors.left: parent.left;
            anchors.right: parent.right;
            // Style
            color: hifi.colors.black;
            wrapMode: Text.WordWrap;
        }

        Rectangle {
            id: failureErrorTextContainer;
            anchors.top: failureHeaderText.bottom;
            anchors.topMargin: 35;
            anchors.left: parent.left;
            anchors.right: parent.right;
            height: failureErrorText.height + 30;
            radius: 4;
            border.width: 2;
            border.color: "#F3808F";
            color: "#FFC3CD";

            AnonymousProRegular {
                id: failureErrorText;
                // Text size
                size: 16;
                // Anchors
                anchors.top: parent.top;
                anchors.topMargin: 15;
                anchors.left: parent.left;
                anchors.leftMargin: 8;
                anchors.right: parent.right;
                anchors.rightMargin: 8;
                height: paintedHeight;
                // Style
                color: hifi.colors.black;
                wrapMode: Text.Wrap;
                verticalAlignment: Text.AlignVCenter;
            }
        }

        Item {
            id: backToMarketplaceButtonContainer;
            // Size
            width: root.width;
            height: 50;
            // Anchors
            anchors.left: parent.left;
            anchors.bottom: parent.bottom;
            anchors.bottomMargin: 16;
            // "Back to Marketplace" button
            HifiControlsUit.Button {
                id: backToMarketplaceButton;
                color: hifi.buttons.noneBorderlessGray;
                colorScheme: hifi.colorSchemes.light;
                anchors.top: parent.top;
                anchors.bottom: parent.bottom;
                anchors.left: parent.left;
                anchors.leftMargin: 16;
                width: parent.width/2 - anchors.leftMargin*2;
                text: "Back to Marketplace";
                onClicked: {
                    sendToScript({method: 'checkout_continueShopping', itemId: itemId});
                }
            }
        }
    }
    //
    // CHECKOUT FAILURE END
    //

    Keys.onPressed: {
        if ((event.key == Qt.Key_F) && (event.modifiers & Qt.ControlModifier)) {
            if (!root.shouldBuyWithControlledFailure) {
                buyButton.text += " DEBUG FAIL ON"
                buyButton.color = hifi.buttons.red;
                root.shouldBuyWithControlledFailure = true;
            } else {
                buyButton.text = (root.isCertified ? ((ownershipStatusReceived && balanceReceived && availableUpdatesReceived) ? (root.alreadyOwned ? "Buy Another" : "Buy"): "--") : "Get Item");
                buyButton.color = hifi.buttons.blue;
                root.shouldBuyWithControlledFailure = false;
            }
        }
    }

    //
    // FUNCTION DEFINITIONS START
    //
    //
    // Function Name: fromScript()
    //
    // Relevant Variables:
    // None
    //
    // Arguments:
    // message: The message sent from the JavaScript, in this case the Marketplaces JavaScript.
    //     Messages are in format "{method, params}", like json-rpc.
    //
    // Description:
    // Called when a message is received from a script.
    //
    function fromScript(message) {
        switch (message.method) {
            case 'updateCheckoutQML':
                root.itemId = message.params.itemId;
                root.itemName = message.params.itemName.trim();
                root.itemPrice = message.params.itemPrice;
                root.itemHref = message.params.itemHref;
                root.referrer = message.params.referrer;
                root.itemAuthor = message.params.itemAuthor;
                root.itemEdition = message.params.itemEdition || -1;
                root.itemType = message.params.itemType || "unknown";
                refreshBuyUI();
            break;
            default:
                console.log('Unrecognized message from marketplaces.js:', JSON.stringify(message));
        }
    }
    signal sendToScript(var message);

    function canBuyAgain() {
        return (root.itemType === "entity" || root.itemType === "wearable" || root.itemType === "contentSet" || root.itemType === "unknown");
    }

    function handleContentSets() {
        if (root.itemType === "contentSet" && !Entities.canReplaceContent()) {
            buyText.text = "The domain owner must enable 'Replace Content' permissions for you in this " +
                "<b>domain's server settings</b> before you can replace this domain's content with <b>" + root.itemName + "</b>";
            buyTextContainer.color = "#FFC3CD";
            buyTextContainer.border.color = "#F3808F";
            buyGlyph.text = hifi.glyphs.alert;
            buyGlyph.size = 54;
        }
    }

    function handleBuyAgainLogic() {
        // If you can buy this item again...
        if (canBuyAgain()) {
            // If you can't afford another copy of the item...
            if (root.balanceAfterPurchase < 0) {
                // If you already own the item...
                if (root.alreadyOwned) {
                    buyText.text = "<b>Your Wallet does not have sufficient funds to purchase this item again.</b>";
                // Else if you don't already own the item...
                } else {
                    buyText.text = "<b>Your Wallet does not have sufficient funds to purchase this item.</b>";
                }
                buyTextContainer.color = "#FFC3CD";
                buyTextContainer.border.color = "#F3808F";
                buyGlyph.text = hifi.glyphs.alert;
                buyGlyph.size = 54;
            // If you CAN afford another copy of the item...
            } else {
                handleContentSets();
            }
        }
    }

    function refreshBuyUI() {
        if (root.isCertified) {
            if (root.ownershipStatusReceived && root.balanceReceived && root.availableUpdatesReceived) {
                buyText.text = "";

                // If the user IS on the checkout page for the updated version of an owned item...
                if (root.isUpdating) {
                    // If the user HAS already selected a specific edition to update...
                    if (root.itemEdition > 0) {
                        buyText.text = "By pressing \"Confirm Update\", you agree to trade in your old item for the updated item that replaces it.";
                        buyTextContainer.color = "#FFFFFF";
                        buyTextContainer.border.color = "#FFFFFF";
                    // Else if the user HAS NOT selected a specific edition to update...
                    } else {
                        viewInMyPurchasesButton.visible = true;

                        handleBuyAgainLogic();
                    }     
                // If the user IS NOT on the checkout page for the updated verison of an owned item...
                // (i.e. they are checking out an item "normally")
                } else {
                    if (root.alreadyOwned) {
                        viewInMyPurchasesButton.visible = true;
                    }
                    
                    handleBuyAgainLogic();
                }
            } else {
                buyText.text = "";
            }
        } else {
            buyText.text = '<i>This type of item cannot currently be certified, so it will not show up in "My Purchases". You can access it again for free from the Marketplace.</i>';
            buyTextContainer.color = hifi.colors.white;
            buyTextContainer.border.color = hifi.colors.white;
            buyGlyph.text = "";
            buyGlyph.size = 0;
        }
    }

    function authSuccessStep() {
        if (!root.debugCheckoutSuccess) {
            root.activeView = "checkoutMain";
            root.ownershipStatusReceived = false;
            Commerce.alreadyOwned(root.itemId);
            root.availableUpdatesReceived = false;
            Commerce.getAvailableUpdates(root.itemId);
            root.balanceReceived = false;
            Commerce.balance();
        } else {
            root.activeView = "checkoutSuccess";
        }
    }

    //
    // FUNCTION DEFINITIONS END
    //
}
