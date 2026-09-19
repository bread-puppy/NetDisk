#pragma once


//包含必要的头文件#include <QOpenGLWidget>// 包含Qt中用于创建OpenGL小部件的头文件
#include <QOpenGLFunctions>// 包含Qt中用于处理OpenGL函数的头文件
#include <QImage>// 包含Qt中用于处理图像的头文件

//自定义OpenGL窗口
class MyOpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT// 声明Qt元对象系统，允许使用信号和槽机制

public:
    //构造函数    MyOpenGLWidget(QWidget *parent = nullptr) : QOpenGLWidget(parent), m_texture(0) {}
    ~MyOpenGLWidget() override;
    //用于设置图像    void slot_setImage(QImage img );

protected:
    //初始化OpenGL
    void initializeGL() override;
    //绘制OpenGL
    void paintGL() override;
    //窗口大小变化
    void resizeGL(int w, int h) override;
    //获取图像缩放后的大小    QSize getImageScaledSize( QSize size);
private:
    QImage m_image;// 存储图像数据的成员变量
    GLuint m_texture;// 存储OpenGL纹理对象标识符的成员变量
};


