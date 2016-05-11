from django.shortcuts import render
from django.http import HttpResponse
from django.template import RequestContext, loader
from .models import Question

# Create your views here.


def index( request ):
    #return HttpResponse( "Hello, world. You're at the polls index." )

    # latest_question_list = Question.objects.order_by( '-pub_date' )[ : 5 ]
    # output = ', '.join( [ p.question_text for p in latest_question_list ] )
    # return HttpResponse( output )

    latest_question_list = Question.objects.order_by('-pub_date')[:5]
    template = loader.get_template( 'polls/index.html' )
    context = RequestContext( request, {
        'latest_question_list': latest_question_list,
    })
    return HttpResponse( template.render(context) )


def detail( request, question_id ):
    return HttpResponse( "You're looking at question %s." % question_id )


def results( request, question_id) :
    response = "You're looking at the results of question %s."
    return HttpResponse( response % question_id )


def vote( request, question_id ):
    return HttpResponse( "You're voting on question %s." % question_id )
