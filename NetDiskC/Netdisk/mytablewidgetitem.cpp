#include "mytablewidgetitem.h"

MyTableWidgetItem::MyTableWidgetItem()
{

}

void MyTableWidgetItem::slot_setInfo(FileInfo &info)
{
    //作为第一列出现 文件名
    m_info=info;
    this->setText(info.name);
    //图标
    if(info.type=="file"){
        this->setIcon(QIcon(":/NetDisk/ui_assets/images/file.png"));
    }
    else{
        this->setIcon(QIcon(":/NetDisk/ui_assets/images/folder.png"));
    }
    //勾选 未打钩
    this->setCheckState(Qt::Unchecked);
}
