#include "logindialog.h"
#include "ui_logindialog.h"
#include<QMessageBox>
#include<QRegExp>
#include<QDebug>

LoginDialog::LoginDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LoginDialog)
{
    ui->setupUi(this);
    setWindowTitle("登录&注册");  //设置标题栏
    ui->tw_page->setCurrentIndex(1);  //窗口默认注册，0是登录
    //去掉按钮的焦点框：点击后按钮会一直带着一圈颜色边框，
    //只有按住时才有按压效果、松开后不留任何边框    const QList<QPushButton*> btns = this->findChildren<QPushButton*>();
    for (QPushButton* b : btns)
        b->setFocusPolicy(Qt::NoFocus);
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

void LoginDialog::on_pb_login_register_clicked()
{
    //注册信息采集
    QString tel=ui->le_tel_register->text();
    QString password=ui->le_password_register->text();
    QString confirm=ui->le_confirm_register->text();
    QString name=ui->le_name_register->text();
    QString tmpName=name;
    //过滤
    //查看是否为空
    if(tel.isEmpty()||password.isEmpty()||confirm.isEmpty()||name.isEmpty()||tmpName.remove(" ").isEmpty()){
        QMessageBox::about(this,"提示","输入内容不能为空");
        return;
    }
    //手机号是否合法,使用正则表达式判断
    QRegExp exp("^1[356789][0-9]\{9\}$");
    bool res=exp.exactMatch(tel);
    if(!res){
        QMessageBox::about(this,"提示","手机号非法");
        return;
    }
    //密码是否过长
    if(password.size()>20){  //length也可以
        QMessageBox::about(this,"提示","密码输入过长，长度应小于20");
        return;
    }
    //密码确认要一致
    if(confirm!=password){
        QMessageBox::about(this,"提示","两次密码输入不一致");
        return;
    }
    //昵称是否过长、敏感词汇过滤
    if(name.length()>10){
        QMessageBox::about(this,"提示","昵称输入过长，长度应小于10");
        return;
    }
    //发信号 Q_EMIT emit
     Q_EMIT SIG_registerCommit(tel,password,name);
}
void LoginDialog::on_pb_login_clicked()
{
    //注册信息采集
    QString tel=ui->le_tel->text();
    QString password=ui->le_password->text();
    //过滤
    //查看是否为空
    if(tel.isEmpty()||password.isEmpty()){
        QMessageBox::about(this,"提示","输入内容不能为空");
        return;
    }
    //手机号是否合法,使用正则表达式判断
    QRegExp exp("^1[356789][0-9]\{9\}$");
    bool res=exp.exactMatch(tel);
    if(!res){
        QMessageBox::about(this,"提示","手机号非法");
        return;
    }
    //密码是否过长
    if(password.size()>20){  //length也可以
        QMessageBox::about(this,"提示","密码输入过长，长度应小于20");
        return;
    }
    //发信号 Q_EMIT emit
    Q_EMIT SIG_loginCommit(tel,password);
}
void LoginDialog::on_pb_clear_clicked()
{
    ui->le_tel->setText("");
    ui->le_password->setText("");
}
void LoginDialog::on_pb_clear_register_clicked()
{
    ui->le_confirm_register->setText("");
    ui->le_name_register->setText("");
    ui->le_password_register->setText("");
    ui->le_tel_register->setText("");
}
