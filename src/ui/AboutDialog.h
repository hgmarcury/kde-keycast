
#pragma once

#include <QDialog>

class QWidget;

class AboutDialog : public QDialog
{
public:
    explicit AboutDialog(
        QWidget *parent = nullptr
    );
};
