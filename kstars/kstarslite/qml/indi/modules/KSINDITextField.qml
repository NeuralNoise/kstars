import QtQuick 2.4
import QtQuick.Layouts 1.2
import "../../constants" 1.0
import org.kde.kirigami 1.0 as Kirigami

import QtQuick 2.4
import QtQuick.Layouts 1.1
import QtQuick.Controls 1.4

Row {
    id: textRow
    spacing: 5 * num.dp
    anchors {
        left: parent.left
        right: parent.right
    }

    property Item textField: field
    property bool isNumber: true // false - text, true - number

    property string deviceName
    property string propName
    property string fieldName

    TextField {
        id: field
    }
    Button {
        text:"Set"
        onClicked: {
            if(isNumber) {
                ClientManagerLite.sendNewINDINumber(deviceName, propName, fieldName, field.text)
            } else {
                ClientManagerLite.sendNewINDIText(deviceName, propName, fieldName, field.text)
            }
            Qt.inputMethod.hide()
        }
    }

    Connections {
        target: ClientManagerLite
        onNewINDINumber: {
            if(isNumber) {
                if(textRow.deviceName == deviceName) {
                    if(textRow.propName == propName) {
                        if(textRow.fieldName == numberName) {
                            field.text = value
                        }
                    }
                }
            }
        }
        onNewINDIText: {
            if(!isNumber) {
                if(textRow.deviceName == deviceName) {
                    if(textRow.propName == propName) {
                        if(textRow.fieldName == fieldName) {
                            field.text = text
                        }
                    }
                }
            }
        }
    }

}